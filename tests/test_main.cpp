/**
 * @file test_main.cpp
 * @brief Simple unit tests for AuraCash components.
 */

#include <iostream>
#include <cassert>
#include <vector>
#include "CoreTypes.h"
#include "AuraHash.h"
#include "EmissionModel.h"
#include "UTXOEngine.h"
#include "Consensus.h"

void test_aurahash() {
    std::cout << "Testing AuraHash..." << std::endl;
    auracash::BlockHeader header;
    header.version = 1;
    header.prevBlockHash.fill(0x01);
    header.merkleRoot.fill(0x02);
    header.timestamp = 1234567890;
    header.targetBits = 0x207fffff;
    header.nonce = 0;

    auto hash1 = auracash::AuraHash(header, 12345);
    auto hash2 = auracash::AuraHash(header, 12345);
    assert(hash1 == hash2 && "AuraHash should be deterministic");

    auto hash3 = auracash::AuraHash(header, 12346);
    assert(hash1 != hash3 && "AuraHash should change with nonce");

    std::cout << "  Passed" << std::endl;
}

void test_emission() {
    std::cout << "Testing EmissionModel..." << std::endl;
    auracash::Amount total = 0;
    double prevReward = 1e9;
    for (uint32_t i = 0; i < 1000000; ++i) {
        auracash::Amount reward = auracash::EmissionModel::ComputeBlockReward(i);
        total += reward;
        // Reward should be non-increasing (actually decreasing)
        if (i > 0) {
            assert(reward <= prevReward && "Reward should not increase");
        }
        prevReward = reward;
        // Reward should never be zero (we set minimum 1 satoshi)
        assert(reward >= 1 && "Reward should be at least 1 satoshi");
    }
    // Total supply after many blocks should be less than max supply
    assert(total <= auracash::MAX_SUPPLY && "Total supply exceeds max cap");
    std::cout << "  Passed, total supply after 1M blocks: " << total / 100'000'000.0 << " XAC" << std::endl;
}

void test_utxo() {
    std::cout << "Testing UTXO Engine..." << std::endl;
    auracash::UTXOSet utxoSet;

    // Create a coinbase transaction
    auracash::Transaction coinbase;
    coinbase.version = 1;
    coinbase.inputs.resize(1);
    coinbase.inputs[0].prevTxHash.fill(0);
    coinbase.inputs[0].prevVout = 0xffffffff;
    coinbase.inputs[0].scriptSig = std::vector<uint8_t>{0x04, 0xff, 0xff, 0x00, 0x1d, 0x01, 0x04};
    coinbase.outputs.resize(1);
    coinbase.outputs[0].value = 50 * 100'000'000ULL; // 50 XAC
    coinbase.outputs[0].scriptPubKey = std::vector<uint8_t>{0x76, 0xa9, 0x14, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x88, 0xac};
    coinbase.lockTime = 0;

    // Add coinbase output to UTXO set (as if it was mined and confirmed)
    auracash::OutPoint outpoint{coinbase.GetHash(), 0};
    utxoSet.AddCoin(outpoint, coinbase.outputs[0]);

    // Create a spending transaction
    auracash::Transaction tx;
    tx.version = 1;
    tx.inputs.resize(1);
    tx.inputs[0].prevTxHash = coinbase.GetHash();
    tx.inputs[0].prevVout = 0;
    tx.inputs[0].scriptSig = std::vector<uint8_t>{0x04, 0xff, 0xff, 0x00, 0x1d, 0x01, 0x04};
    tx.inputs[0].sequence = 0xffffffff;
    tx.outputs.resize(2);
    tx.outputs[0].value = 25 * 100'000'000ULL; // 25 XAC
    tx.outputs[0].scriptPubKey = std::vector<uint8_t>{0x76, 0xa9, 0x14, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x88, 0xac};
    tx.outputs[1].value = 24 * 100'000'000ULL; // 24 XAC (1 XAC fee)
    tx.outputs[1].scriptPubKey = std::vector<uint8_t>{0x76, 0xa9, 0x14, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x88, 0xac};
    tx.lockTime = 0;

    // Validate the transaction (should succeed)
    bool valid = utxoSet.ValidateAndApplyInputs(tx);
    assert(valid && "Transaction should be valid");
    // Commit to actually spend the coin
    utxoSet.CommitBlock();

    // Now try to spend the same input again (should fail)
    auracash::Transaction tx2;
    tx2.version = 1;
    tx2.inputs.resize(1);
    tx2.inputs[0].prevTxHash = coinbase.GetHash();
    tx2.inputs[0].prevVout = 0;
    tx2.inputs[0].scriptSig = std::vector<uint8_t>{0x04, 0xff, 0xff, 0x00, 0x1d, 0x01, 0x04};
    tx2.inputs[0].sequence = 0xffffffff;
    tx2.outputs.resize(1);
    tx2.outputs[0].value = 49 * 100'000'000ULL;
    tx2.outputs[0].scriptPubKey = std::vector<uint8_t>{0x76, 0xa9, 0x14, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x88, 0xac};
    tx2.lockTime = 0;

    bool valid2 = utxoSet.ValidateAndApplyInputs(tx2);
    assert(!valid2 && "Double-spend should be detected");
    std::cout << "  Passed" << std::endl;
}

void test_consensus() {
    std::cout << "Testing Consensus (simple)..." << std::endl;
    auracash::UTXOSet utxoSet;
    auracash::ConsensusEngine consensus(utxoSet);

    // Genesis block
    auracash::Block genesis = auracash::ConsensusEngine::GetGenesisBlock();
    std::cout << "Genesis block hash: ";
    for (auto b : genesis.GetHash()) {
        printf("%02x", b);
    }
    std::cout << std::endl;

    auto res = consensus.ValidateBlock(genesis, nullptr);
    if (!res.ok) {
        std::cerr << "Genesis block validation failed: " << res.reason << std::endl;
    }
    assert(res.ok && "Genesis block should be valid");
    std::cout << "  Passed" << std::endl;
}

int main() {
    test_aurahash();
    test_emission();
    test_utxo();
    test_consensus();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}