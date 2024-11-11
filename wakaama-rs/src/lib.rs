#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::cmp::min;
use std::sync::{Arc, Mutex};

include!(concat!(env!("OUT_DIR"), "/wakaama_bindings.rs"));


lazy_static::lazy_static! {
    static ref SEND_BUF_MUTEX: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
    static ref SERVER_INSTANCE: Arc<Mutex<Option<ServerInternal>>> = Arc::new(Mutex::new(None));
}

#[no_mangle]
pub extern "C" fn lwm2m_session_is_equal(
    session1: *mut ::std::os::raw::c_void,
    session2: *mut ::std::os::raw::c_void,
    _userData: *mut ::std::os::raw::c_void,
) -> bool {
    let session1 = session1 as *const i32;
    let session2 = session2 as *const i32;
    session1 == session2
}

//
const MAX_PACKET_SIZE: usize = 2048;


/// # Safety
///
/// This function is called by Wakaama internally. Don't call it manually.
#[no_mangle]
pub unsafe extern "C" fn lwm2m_buffer_send(
    _sessionH: *mut ::std::os::raw::c_void,
    buffer: *mut u8,
    length: usize,
    _userData: *mut ::std::os::raw::c_void,
) -> u8 {
    let length = min(length, MAX_PACKET_SIZE);
    let mut buf = SEND_BUF_MUTEX.lock().unwrap();
    buf.clear();
    buf.resize(length, 0);
    unsafe {
        buf.copy_from_slice(std::slice::from_raw_parts(buffer, length));
    }
    COAP_NO_ERROR as u8
}

pub trait MonitoringHandler {
    fn monitor(&mut self, client_id: u16);
}


struct ServerInternal {
    pub context: *mut lwm2m_context_t,
    monitoring_handler: Option<Arc<Mutex<dyn MonitoringHandler>>>,
}

unsafe impl Sync for ServerInternal {}
unsafe impl Send for ServerInternal {}

fn create_instance() {
    if SERVER_INSTANCE.lock().unwrap().is_none() {
        SERVER_INSTANCE.lock().unwrap().replace(ServerInternal {
            context: unsafe { lwm2m_init(std::ptr::null_mut()) },
            monitoring_handler: None,
        });
    }
}


pub fn register_monitoring_handler(handler: Arc<Mutex<dyn MonitoringHandler>>) {
    let mut inst = SERVER_INSTANCE.lock().unwrap();
    let inst = inst.as_mut().unwrap();
    unsafe {
        
        lwm2m_set_monitoring_callback(inst.context, Some(monitoring_callback), std::ptr::null_mut());
    }
    inst.monitoring_handler = Some(handler)
}

pub fn handle_callback(id: u16) {
    let inst = &mut SERVER_INSTANCE.lock().unwrap();
    let inst = inst.as_mut().unwrap();
    let handler = &inst.monitoring_handler;
    if let Some(handler) = handler {
        handler.lock().unwrap().monitor(id);
    }
}

fn handle_packet(mut buffer: Vec<u8>) {
    let mut inst = SERVER_INSTANCE.lock().unwrap();
    let inst = inst.as_mut().unwrap();
    unsafe {
        let buf = buffer.as_mut_ptr();
        let len = buffer.len();
        let session = std::ptr::null_mut();
        lwm2m_handle_packet(inst.context, buf, len, session);
    }
}

extern "C" fn monitoring_callback(
    _contextP: *mut lwm2m_context_t,
    clientID: u16,
    _uriP: *mut lwm2m_uri_t,
    _status: ::std::os::raw::c_int,
    _block_info: *mut block_info_t,
    _format: lwm2m_media_type_t,
    _data: *mut u8,
    _dataLength: usize,
    _userData: *mut ::std::os::raw::c_void,
) {
    handle_callback(clientID);
}

#[cfg(test)]
mod tests {
    use super::*;

    use coap_lite::ResponseType::Created;
    use coap_lite::{
        CoapOption, CoapRequest, ContentFormat, MessageClass, Packet, RequestType as Method,
    };
    use std::net::SocketAddr;
    use std::thread;

    struct TestingMonitoringHandler {
        object_name: String,
        result: String
    }

    impl TestingMonitoringHandler {
        fn new(object_name: String) -> TestingMonitoringHandler {
            TestingMonitoringHandler {
                object_name,
                result: "".to_string(),
            }
        }
    }
    impl MonitoringHandler for TestingMonitoringHandler {
        fn monitor(&mut self, _client_id: u16) {
            self.result = format!("Called MonitoringHandler::monitor from {:}", self.object_name);

        }
    }

    #[test]
    fn test_callback() {
        
        let my_monitoring_handler = Arc::new(Mutex::new(TestingMonitoringHandler::new("object A".to_string())));

        register_monitoring_handler(Arc::clone(&my_monitoring_handler) as _);

        let packet = coap_client_for_tests();

        
        handle_packet(packet);

        let response = get_response_from_wakaama();

        assert_eq!(response.header.code, MessageClass::Response(Created));
        assert_eq!(my_monitoring_handler.lock().unwrap().result, "Called MonitoringHandler::monitor from object A");
    }
    
    #[test]
    fn test_callback_multithreaded() {
        create_instance();
        let num_servers = 3;
        let mut servers = Vec::with_capacity(num_servers);
        let mut monitoring_handlers = Vec::with_capacity(num_servers);
        for i in 0..servers.capacity() {
            let my_monitoring_handler = Arc::new(Mutex::new(TestingMonitoringHandler::new(format!("object {:}", i))));
            monitoring_handlers.push(my_monitoring_handler.clone());
            register_monitoring_handler(Arc::clone(&my_monitoring_handler.clone()) as _);
            servers.push(
                thread::spawn(move || {
                    handle_packet(coap_client_for_tests());
                }));
        }
        
        for s in servers {
            s.join().unwrap();
        }

        for (i,h) in monitoring_handlers.iter().enumerate() {
            assert_eq!(h.lock().unwrap().result, format!("Called MonitoringHandler::monitor from object {:}", i));
        }
    }

    fn get_response_from_wakaama() -> Packet {
        let actual = SEND_BUF_MUTEX.lock().unwrap();
        let actual = actual.as_slice();
        Packet::from_bytes(actual).unwrap()
    }

    fn coap_client_for_tests() -> Vec<u8> {
        let mut request: CoapRequest<SocketAddr> = CoapRequest::new();

        request.set_method(Method::Post);
        request.set_path("/rd");
        request.message.add_option(
            CoapOption::UriQuery,
            "ep=rs-test-client".as_bytes().to_vec(),
        );
        request
            .message
            .add_option(CoapOption::UriQuery, "lt=43200".as_bytes().to_vec());
        request
            .message
            .add_option(CoapOption::UriQuery, "lwm2m=1.1".as_bytes().to_vec());
        request
            .message
            .add_option(CoapOption::UriQuery, "b=U".as_bytes().to_vec());
        request.message.payload = b"</1/1>,</2/1>,</3/0>".to_vec();
        request
            .message
            .set_content_format(ContentFormat::ApplicationLinkFormat);

        request.message.to_bytes().unwrap()
    }
}
