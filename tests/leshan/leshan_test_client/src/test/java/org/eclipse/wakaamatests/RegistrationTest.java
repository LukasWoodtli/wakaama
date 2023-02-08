package org.eclipse.wakaamatests;

import org.eclipse.californium.core.network.config.NetworkConfig;
import org.eclipse.leshan.client.californium.LeshanClient;
import org.eclipse.leshan.client.californium.LeshanClientBuilder;
import org.eclipse.leshan.client.object.Device;
import org.eclipse.leshan.client.object.Security;
import org.eclipse.leshan.client.object.Server;
import org.eclipse.leshan.client.resource.ObjectsInitializer;
import org.eclipse.leshan.client.servers.ServerIdentity;
import org.eclipse.leshan.core.LwM2mId;
import org.eclipse.leshan.core.request.BindingMode;
import org.junit.jupiter.api.Test;

import java.util.Map;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class RegistrationTest {

    @Test
    void register() throws InterruptedException {
        String endpoint = "leshan-1-testing-client";
        LeshanClientBuilder builder = new LeshanClientBuilder(endpoint);

        // leehan 1
        NetworkConfig networkConfig = NetworkConfig.getStandard();
        networkConfig.setInt("PREFERRED_BLOCK_SIZE", 16);
        networkConfig.setInt("MAX_MESSAGE_SIZE", 16);
        networkConfig.setBoolean("DEFAULT_BLOCKWISE_STRICT_BLOCK2_OPTION", true);
        builder.setCoapConfig(networkConfig);

        ObjectsInitializer initializer = new ObjectsInitializer(); //new ObjectsInitializer(new StaticModel(models));
        initializer.setInstancesForObject(LwM2mId.SECURITY, Security.noSec("coap://localhost:5683", 12345));
        // leshan 1
        initializer.setInstancesForObject(LwM2mId.SERVER, new Server(12345, 5 * 60, BindingMode.U, false));
        initializer.setInstancesForObject(LwM2mId.DEVICE, new Device("Eclipse Leshan", "model12345", "12345", "U"));

        builder.setObjects(initializer.createAll());

        LeshanClient client = builder.build();

        client.start();

        // wait for registration to complete
        boolean registered = false;
        final long timeout = System.currentTimeMillis() + 1000;

        while (!registered && System.currentTimeMillis() < timeout)
        {
            registered = client.getRegisteredServers().size() > 0;
            if (registered) {
                break;
            }
            Thread.sleep(20);
        }

        assertTrue(registered);

        Map<String, ServerIdentity> servers = client.getRegisteredServers();
        assertEquals(1, servers.size());

        Map.Entry<String, ServerIdentity> entry = servers.entrySet().iterator().next();
        String key = entry.getKey();
        ServerIdentity serverIdentity = entry.getValue();

        assertEquals("/rd/0", key);
        assertEquals("coap://localhost:5683[LWM2M_SERVER 12345]", serverIdentity.toString());
    }
}
