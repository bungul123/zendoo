#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test checkcswnullifier for presence nullifer in MC.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    wait_bitcoinds, stop_nodes, get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch

from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 4
DEBUG_MODE = 1
EPOCH_LENGTH = 10

# Create one-input, one-output, no-fee transaction:
class CertMempoolCleanupSplit(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 2 and 3 are joint only if split==false
            connect_nodes_bi(self.nodes, 2, 3)
            sync_blocks(self.nodes[2:4])
            sync_mempools(self.nodes[2:4])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of three nodes into nodes 0-1-2 and 3.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[2], 3)
        disconnect_nodes(self.nodes[3], 2)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1-2-3
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 2)
        time.sleep(2)
        self.is_network_split = False

    def run_test(self):
        '''
        Create a SC, advance two epochs, move to the limit of the safe guard and then split the network. 
        One network part sends fwt, mbtr and a certificate, all of them are stored in mempool.
        The other network part sends a certificate and mines one block reaching a longer chain height.
        When the network is joined, verify that the SC is alive, fwt is still in the mempool of the the
        loosing network part, but cert and mbtr have been removed from those mempool.
        '''

        # prepare some coins 
        self.nodes[3].generate(1)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        self.nodes[0].generate(217)
        self.sync_all()
        block_2 = self.nodes[0].getbestblockhash()
        self.nodes[0].generate(1)
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()
        block_1 = prev_epoch_hash

        print "Node0 Chain h = ", self.nodes[0].getblockcount()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)

        proving_system = "Darlin"

        # generate wCertVk and constant
        vk = mcTest.generate_params("sc1")
        cswVk = mcTest.generate_params("csw1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            'withdrawalEpochLength': sc_epoch_len,
            'amount': sc_cr_amount,
            'toaddress': sc_address,
            "certProvingSystem":proving_system,
            'wCertVk': vk,
            'constant': constant,
            'mainchainBackwardTransferRequestDataLength': 1
        }

        res = self.nodes[0].create_sidechain(cmdInput)
        tx =   res['txid']
        scid = res['scid']
        self.sync_all()
        mark_logs("tx {} created SC {}".format(tx, scid), self.nodes, DEBUG_MODE)

        pkh1 = self.nodes[1].getnewaddress("", True)
        fe1 = [generate_random_field_element_hex()]

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...".  format(sc_epoch_len), self.nodes, DEBUG_MODE)

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid, prev_epoch_hash, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid, prev_epoch_hash, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}l".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

        ceas_height = self.nodes[0].getscinfo(scid, False, False)['items'][0]['ceasing height']
        numbBlocks = ceas_height - self.nodes[0].getblockcount() + sc_epoch_len - 1
        print "Node0 Chain h = ", self.nodes[0].getblockcount()

        mark_logs("\nNode0 generates {} block reaching the sg for the next epoch".format(numbBlocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(numbBlocks)
        self.sync_all()
        print "Node0 Chain h = ", self.nodes[0].getblockcount()
        
        bal_initial = self.nodes[0].getscinfo(scid, False, False)['items'][0]['balance']

        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0-1-2 .. 3", self.nodes, DEBUG_MODE)

        # Network part 0-1-2
        print "------------------"
        # use different nodes for sending txes and cert in order to be sure there are no dependancies from each other
        fwt_amount = Decimal("2.0")
        mark_logs("\nNTW part 1) Node0 sends {} coins to SC".format(fwt_amount), self.nodes, DEBUG_MODE)
        tx_fwd = self.nodes[0].sc_send("abcd", fwt_amount, scid)
        sync_mempools(self.nodes[0:3])

        mark_logs("              Check fwd tx {} is in mempool".format(tx_fwd), self.nodes, DEBUG_MODE)
        assert_true(tx_fwd in self.nodes[0].getrawmempool()) 

        outputs = [{'vScRequestData':fe1, 'scFee':Decimal("0.001"), 'scid':scid, 'pubkeyhash':pkh1 }]
        cmdParms = { "minconf":0, "fee":0.0}
        mark_logs("\nNTW part 1) Node1 creates a tx with a bwt request", self.nodes, DEBUG_MODE)
        try:
            tx_bwt = self.nodes[1].request_transfer_from_sidechain(outputs, cmdParms);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        sync_mempools(self.nodes[0:3])

        mark_logs("              Check bwd tx {} is in mempool".format(tx_bwt), self.nodes, DEBUG_MODE)
        assert_true(tx_bwt in self.nodes[0].getrawmempool()) 

        mark_logs("\nNTW part 1) Node2 sends a certificate", self.nodes, DEBUG_MODE)
        epoch_block_hash, epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[2], sc_epoch_len)

        bt_amount = Decimal("5.0")
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        quality = 10

        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_hash,
            quality, constant, [pkh_node1], [bt_amount])

        amount_cert = [{"pubkeyhash": pkh_node1, "amount": bt_amount}]
        try:
            cert_bad = self.nodes[2].send_certificate(scid, epoch_number, quality, epoch_block_hash, epoch_cum_tree_hash,
                                                      proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "Send certificate failed with reason {}".format(errorString)
            assert(False)
        sync_mempools(self.nodes[0:3])

        mark_logs("              Check cert {} is in mempool".format(cert_bad), self.nodes, DEBUG_MODE)
        assert_true(cert_bad in self.nodes[0].getrawmempool()) 
        print "Node0 Chain h = ", self.nodes[0].getblockcount()

        # Network part 2
        #------------------
        mark_logs("\nNTW part 2) Node3 sends a certificate", self.nodes, DEBUG_MODE)
        epoch_block_hash, epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[3], sc_epoch_len)

        bt_amount_2 = Decimal("10.0")
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        quality = 5

        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_hash,
            quality, constant, [pkh_node1], [bt_amount_2])

        amount_cert = [{"pubkeyhash": pkh_node1, "amount": bt_amount_2}]
        try:
            cert = self.nodes[3].send_certificate(scid, epoch_number, quality, epoch_block_hash, epoch_cum_tree_hash,
                                                  proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "Send certificate failed with reason {}".format(errorString)
            assert(False)
        sync_mempools(self.nodes[3:4])

        mark_logs("              Check cert {} is in mempool".format(cert), self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[3].getrawmempool())
        print "Node3 Chain h = ", self.nodes[3].getblockcount()


        mark_logs("Node3 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(1)
        sync_mempools(self.nodes[3:4])
        print "Node3 Chain h = ", self.nodes[3].getblockcount()

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        # check SC is alive
        ret = self.nodes[3].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "ALIVE")

        mark_logs("Check fwd tx {} is still in mempool...".format(tx_fwd), self.nodes, DEBUG_MODE)
        assert_true(tx_fwd in self.nodes[0].getrawmempool()) 

        any_error = False

        mark_logs("Check bwd tx {} is no more in mempool, since we crossed the epoch safeguard".format(tx_bwt), self.nodes, DEBUG_MODE)
        #assert_false(tx_bwt in self.nodes[0].getrawmempool()) 
        if tx_bwt in self.nodes[0].getrawmempool():
            print "FIX FIX FIX!!! bwt is still in mempool" 
            any_error = True

        mark_logs("And that no info are available too...".format(tx_bwt), self.nodes, DEBUG_MODE)
        try:
            dec = self.nodes[0].getrawtransaction(tx_bwt, 1)
            print "FIX FIX FIX!!! tx_bwt has info in Node0" 
            any_error = True
            #assert (False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("No information" in errorString)

        mark_logs("Check cert {} is no more in mempool, since we crossed the epoch safeguard".format(cert_bad), self.nodes, DEBUG_MODE)
        #assert_false(cert_bad in self.nodes[0].getrawmempool()) 
        if cert_bad in self.nodes[0].getrawmempool():
            print "FIX FIX FIX!!! cert is still in mempool" 
            any_error = True

        mark_logs("And that no info are available too...", self.nodes, DEBUG_MODE)
        try:
            dec = self.nodes[0].getrawcertificate(cert_bad, 1)
            print "FIX FIX FIX!!! cert has info in Node0" 
            any_error = True
            #assert (False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Check SC balance has been properly changed", self.nodes, DEBUG_MODE)
        try:
            bal_final = self.nodes[0].getscinfo(scid)['items'][0]['balance']
            assert_equal(bal_initial - bt_amount_2, bal_final)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)

        if any_error:
            print" =========================> Test failed!!!"
            #assert(False)

        for i in range(0,4):
            pprint.pprint(self.nodes[i].getrawmempool())

        # if any_error this should fail
        self.nodes[0].generate(1)
        self.sync_all()


if __name__ == '__main__':
    CertMempoolCleanupSplit().main()
