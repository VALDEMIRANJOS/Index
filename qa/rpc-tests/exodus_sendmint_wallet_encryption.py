#!/usr/bin/env python3
from time import sleep
from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import ExodusTestFramework
from test_framework.util import (assert_equal, assert_raises_message, start_node, assert_equal, bitcoind_processes)

class ExodusSendMintWalletEncrytionTest(ExodusTestFramework):
    def run_test(self):
        super().run_test()

        sigma_start_block = 500

        self.nodes[0].generatetoaddress(100, self.addrs[0])
        self.nodes[0].generate(sigma_start_block - self.nodes[0].getblockcount())

        self.nodes[0].exodus_sendissuancefixed(
            self.addrs[0], 1, 1, 0, '', '', 'Sigma', '', '', '1000000', 1
        )
        self.nodes[0].generate(1)
        sigmaProperty = 3

        self.nodes[0].exodus_sendcreatedenomination(self.addrs[0], sigmaProperty, '1')
        self.nodes[0].generate(10)

        passphase = 'test'
        self.nodes[0].encryptwallet(passphase)
        bitcoind_processes[0].wait()
        self.nodes[0] = start_node(0, self.options.tmpdir, ['-exodus'])

        # try to mint using encrypted wallet
        assert_raises_message(
            JSONRPCException,
            'Wallet locked',
            self.nodes[0].exodus_sendmint, self.addrs[0], sigmaProperty, {"0":1}
        )

        self.nodes[0].walletpassphrase(passphase, 3)

        self.nodes[0].exodus_sendmint(self.addrs[0], sigmaProperty, {"0":1})

        sleep(3)

        assert_raises_message(
            JSONRPCException,
            'Wallet locked',
            self.nodes[0].exodus_sendmint, self.addrs[0], sigmaProperty, {"0":1}
        )

if __name__ == "__main__":
    ExodusSendMintWalletEncrytionTest().main()