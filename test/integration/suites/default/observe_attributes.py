# -*- coding: utf-8 -*-
#
# Copyright 2017 AVSystem <avsystem@avsystem.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import socket

from framework.lwm2m_test import *

from . import access_control as ac


class ObserveAttributesTest(test_suite.Lwm2mSingleServerTest,
                            test_suite.Lwm2mDmOperations):
    def runTest(self):
        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)
        # Observe: Counter
        counter_pkt = self.observe(self.serv, oid=1337, iid=0, rid=1)

        # no message should arrive here
        with self.assertRaises(socket.timeout):
            self.serv.recv(timeout_s=5)

        # Write Attributes
        self.write_attributes(self.serv, oid=1337, iid=0, rid=1, query=['pmax=2'])

        # now we should get notifications, even though nothing changed
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)

        # and another one
        pkt = self.serv.recv(timeout_s=3)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, counter_pkt.content)


class ObserveResourceWithEmptyHandler(test_suite.Lwm2mSingleServerTest,
                                      test_suite.Lwm2mDmOperations):
    def runTest(self):
        # See T832. resource_read handler implemented as 'return 0;'
        # used to cause segfault when observed.

        # Create object
        self.create_instance(self.serv, oid=1337, iid=0)

        # Observe: Empty
        self.observe(self.serv, oid=1337, iid=0, rid=5,
                     expect_error_code=coap.Code.RES_INTERNAL_SERVER_ERROR)
        # hopefully that does not segfault


class ObserveWithMultipleServers(ac.AccessControl.Test):
    def runTest(self):
        self.create_instance(server=self.servers[1], oid=1337, iid=0)
        self.update_access(server=self.servers[1], oid=1337, iid=0,
                           acl=[ac.make_acl_entry(1, ac.ACCESS_MASK_READ | ac.ACCESS_MASK_EXECUTE),
                                ac.make_acl_entry(2, ac.ACCESS_MASK_OWNER)])
        # Observe: Counter
        self.observe(self.servers[1], oid=1337, iid=0, rid=1)
        # Expecting silence
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.write_attributes(self.servers[1], oid=1337, iid=0, rid=1, query=['gt=1'])
        with self.assertRaises(socket.timeout):
            self.servers[1].recv(timeout_s=2)

        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2)  # ++counter
        self.execute_resource(self.servers[0], oid=1337, iid=0, rid=2)  # ++counter
        pkt = self.servers[1].recv(timeout_s=2)
        self.assertEqual(pkt.code, coap.Code.RES_CONTENT)
        self.assertEqual(pkt.content, b'2')
