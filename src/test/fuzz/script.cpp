// Copyright (c) 2019-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <compressor.h>
#include <core_io.h>
#include <core_memusage.h>
#include <policy/policy.h>
#include <pubkey.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <streams.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/fuzz/util.h>
#include <univalue.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

void initialize_script()
{
    // Fuzzers using pubkey must hold an ECCVerifyHandle.
    static const ECCVerifyHandle verify_handle;

    SelectParams(CBaseChainParams::REGTEST);
}

FUZZ_TARGET_INIT(script, initialize_script)
{
    FuzzedDataProvider fuzzed_data_provider(buffer.data(), buffer.size());
    const std::optional<CScript> script_opt = ConsumeDeserializable<CScript>(fuzzed_data_provider);
    if (!script_opt) return;
    const CScript script{*script_opt};

    std::vector<unsigned char> compressed;
    if (CompressScript(script, compressed)) {
        const unsigned int size = compressed[0];
        compressed.erase(compressed.begin());
        assert(size <= 5);
        CScript decompressed_script;
        const bool ok = DecompressScript(decompressed_script, size, compressed);
        assert(ok);
        assert(script == decompressed_script);
    }

    CTxDestination address;
    (void)ExtractDestination(script, address);

    TxoutType type_ret;
    std::vector<CTxDestination> addresses;
    int required_ret;
    (void)ExtractDestinations(script, type_ret, addresses, required_ret);

    const FlatSigningProvider signing_provider;
    (void)InferDescriptor(script, signing_provider);

    (void)IsSegWitOutput(signing_provider, script);

    (void)IsSolvable(signing_provider, script);

    TxoutType which_type;
    bool is_standard_ret = IsStandard(script, which_type);
    if (!is_standard_ret) {
        assert(which_type == TxoutType::NONSTANDARD ||
               which_type == TxoutType::NULL_DATA ||
               which_type == TxoutType::MULTISIG);
    }
    if (which_type == TxoutType::NONSTANDARD) {
        assert(!is_standard_ret);
    }
    if (which_type == TxoutType::NULL_DATA) {
        assert(script.IsUnspendable());
    }
    if (script.IsUnspendable()) {
        assert(which_type == TxoutType::NULL_DATA ||
               which_type == TxoutType::NONSTANDARD);
    }

    (void)RecursiveDynamicUsage(script);

    std::vector<std::vector<unsigned char>> solutions;
    (void)Solver(script, solutions);

    (void)script.HasValidOps();
    (void)script.IsPayToScriptHash(false);
    (void)script.IsPayToWitnessScriptHash(false);
    (void)script.IsPushOnly();
    (void)script.GetSigOpCount(/* fAccurate= */ false);

    (void)FormatScript(script);
    (void)ScriptToAsmStr(script, false);
    (void)ScriptToAsmStr(script, true);

    UniValue o1(UniValue::VOBJ);
    ScriptPubKeyToUniv(script, o1, true);
    UniValue o2(UniValue::VOBJ);
    ScriptPubKeyToUniv(script, o2, false);
    UniValue o3(UniValue::VOBJ);
    ScriptToUniv(script, o3, true);
    UniValue o4(UniValue::VOBJ);
    ScriptToUniv(script, o4, false);

    {
        const std::vector<uint8_t> bytes = ConsumeRandomLengthByteVector(fuzzed_data_provider);
        // DecompressScript(..., ..., bytes) is not guaranteed to be defined if the bytes vector is too short
        if (bytes.size() >= 32) {
            CScript decompressed_script;
            DecompressScript(decompressed_script, fuzzed_data_provider.ConsumeIntegral<unsigned int>(), bytes);
        }
    }

    const std::optional<CScript> other_script = ConsumeDeserializable<CScript>(fuzzed_data_provider);
    if (other_script) {
        {
            CScript script_mut{script};
            (void)FindAndDelete(script_mut, *other_script);
        }
        const std::vector<std::string> random_string_vector = ConsumeRandomLengthStringVector(fuzzed_data_provider);
        const uint32_t u32{fuzzed_data_provider.ConsumeIntegral<uint32_t>()};
        const uint32_t flags{u32 | SCRIPT_VERIFY_P2SH};
        {
            CScriptWitness wit;
            for (const auto& s : random_string_vector) {
                wit.stack.emplace_back(s.begin(), s.end());
            }
            (void)CountWitnessSigOps(script, *other_script, &wit, flags);
            wit.SetNull();
        }
    }

    (void)GetOpName(ConsumeOpcodeType(fuzzed_data_provider));
    (void)ScriptErrorString(static_cast<ScriptError>(fuzzed_data_provider.ConsumeIntegralInRange<int>(0, SCRIPT_ERR_ERROR_COUNT)));

    {
        const std::vector<uint8_t> bytes = ConsumeRandomLengthByteVector(fuzzed_data_provider);
        CScript append_script{bytes.begin(), bytes.end()};
        append_script << fuzzed_data_provider.ConsumeIntegral<int64_t>();
        append_script << ConsumeOpcodeType(fuzzed_data_provider);
        append_script << CScriptNum{fuzzed_data_provider.ConsumeIntegral<int64_t>()};
        append_script << ConsumeRandomLengthByteVector(fuzzed_data_provider);
    }

    {
        WitnessUnknown witness_unknown_1{};
        witness_unknown_1.version = fuzzed_data_provider.ConsumeIntegral<uint32_t>();
        const std::vector<uint8_t> witness_unknown_program_1 = fuzzed_data_provider.ConsumeBytes<uint8_t>(40);
        witness_unknown_1.length = witness_unknown_program_1.size();
        std::copy(witness_unknown_program_1.begin(), witness_unknown_program_1.end(), witness_unknown_1.program);

        WitnessUnknown witness_unknown_2{};
        witness_unknown_2.version = fuzzed_data_provider.ConsumeIntegral<uint32_t>();
        const std::vector<uint8_t> witness_unknown_program_2 = fuzzed_data_provider.ConsumeBytes<uint8_t>(40);
        witness_unknown_2.length = witness_unknown_program_2.size();
        std::copy(witness_unknown_program_2.begin(), witness_unknown_program_2.end(), witness_unknown_2.program);

        (void)(witness_unknown_1 == witness_unknown_2);
        (void)(witness_unknown_1 < witness_unknown_2);
    }

    {
        const CTxDestination tx_destination_1 = ConsumeTxDestination(fuzzed_data_provider);
        const CTxDestination tx_destination_2 = ConsumeTxDestination(fuzzed_data_provider);
        (void)(tx_destination_1 == tx_destination_2);
        (void)(tx_destination_1 < tx_destination_2);
    }
}
