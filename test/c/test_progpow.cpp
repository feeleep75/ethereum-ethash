#include <iomanip>
#include <libethash/fnv.h>
#include <libethash/ethash.h>
#include <libethash/internal.h>
#include <libethash/io.h>

#ifdef WITH_CRYPTOPP

#include <libethash/sha3_cryptopp.h>

#else
#include <libethash/sha3.h>
#endif // WITH_CRYPTOPP

#ifdef _WIN32
#include <windows.h>
#include <Shlobj.h>
#endif

#include <iostream>
#include <fstream>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

using namespace std;
using byte = uint8_t;
using bytes = std::vector<byte>;
namespace fs = boost::filesystem;

// Just an alloca "wrapper" to silence uint64_t to size_t conversion warnings in windows
// consider replacing alloca calls with something better though!
#define our_alloca(param__) alloca((size_t)(param__))

// some functions taken from eth::dev for convenience.
static std::string bytesToHexString(const uint8_t *str, const uint64_t s)
{
	std::ostringstream ret;

	for (size_t i = 0; i < s; ++i)
		ret << std::hex << std::setfill('0') << std::setw(2) << std::nouppercase << (int) str[i];

	return ret.str();
}

static std::string blockhashToHexString(ethash_h256_t* _hash)
{
	return bytesToHexString((uint8_t*)_hash, 32);
}

static int fromHex(char _i)
{
	if (_i >= '0' && _i <= '9')
		return _i - '0';
	if (_i >= 'a' && _i <= 'f')
		return _i - 'a' + 10;
	if (_i >= 'A' && _i <= 'F')
		return _i - 'A' + 10;

	BOOST_REQUIRE_MESSAGE(false, "should never get here");
	return -1;
}

static bytes hexStringToBytes(std::string const& _s)
{
	unsigned s = (_s[0] == '0' && _s[1] == 'x') ? 2 : 0;
	std::vector<uint8_t> ret;
	ret.reserve((_s.size() - s + 1) / 2);

	if (_s.size() % 2)
		try
		{
			ret.push_back(fromHex(_s[s++]));
		}
		catch (...)
		{
			ret.push_back(0);
		}
	for (unsigned i = s; i < _s.size(); i += 2)
		try
		{
			ret.push_back((byte)(fromHex(_s[i]) * 16 + fromHex(_s[i + 1])));
		}
		catch (...){
			ret.push_back(0);
		}
	return ret;
}

static ethash_h256_t stringToBlockhash(std::string const& _s)
{
	ethash_h256_t ret;
	bytes b = hexStringToBytes(_s);
	memcpy(&ret, b.data(), b.size());
	return ret;
}

/* ProgPoW */

static void ethash_keccakf800(uint32_t state[25])
{
    for (int i = 0; i < 22; ++i)
        keccak_f800_round(state, i);
}

BOOST_AUTO_TEST_CASE(test_progpow_math)
{
	typedef struct {
		uint32_t a;
		uint32_t b;
		uint32_t exp;
	} mytest;

	mytest tests[] = {
		{20, 22, 42},
		{70000, 80000, 1305032704},
		{70000, 80000, 1},
		{1, 2, 1},
		{3, 10000, 196608},
		{3, 0, 3},
		{3, 6, 2},
		{3, 6, 7},
		{3, 6, 5},
		{0, 0xffffffff, 32},
		{3 << 13, 1 << 5, 3},
		{22, 20, 42},
		{80000, 70000, 1305032704},
		{80000, 70000, 1},
		{2, 1, 1},
		{10000, 3, 80000},
		{0, 3, 0},
		{6, 3, 2},
		{6, 3, 7},
		{6, 3, 5},
		{0, 0xffffffff, 32},
		{3 << 13, 1 << 5, 3},
	};

	for (int i = 0; i < sizeof(tests) / sizeof(mytest); i++) {
		uint32_t res = progpowMath(tests[i].a, tests[i].b, (uint32_t)i);
		BOOST_REQUIRE_EQUAL(res, tests[i].exp);
	}
}

BOOST_AUTO_TEST_CASE(test_progpow_merge)
{
	typedef struct {
		uint32_t a;
		uint32_t b;
		uint32_t exp;
	} mytest;
	mytest tests[] = {
		{1000000, 101, 33000101},
		{2000000, 102, 66003366},
		{3000000, 103, 2999975},
		{4000000, 104, 4000104},
		{1000000, 0, 33000000},
		{2000000, 0, 66000000},
		{3000000, 0, 3000000},
		{4000000, 0, 4000000},
	};
	for (int i = 0; i < sizeof(tests) / sizeof(mytest); i++) {
		uint32_t res = tests[i].a;
		merge(&res, tests[i].b, (uint32_t)i);
		BOOST_REQUIRE_EQUAL(res, tests[i].exp);
	}
}

BOOST_AUTO_TEST_CASE(test_progpow_keccak)
{
	// Test vectors from
	// https://github.com/XKCP/XKCP/blob/master/tests/TestVectors/KeccakF-800-IntermediateValues.txt.
	uint32_t state[25] = {};
	const uint32_t expected_state_0[] = {0xE531D45D, 0xF404C6FB, 0x23A0BF99, 0xF1F8452F, 0x51FFD042,
		0xE539F578, 0xF00B80A7, 0xAF973664, 0xBF5AF34C, 0x227A2424, 0x88172715, 0x9F685884,
		0xB15CD054, 0x1BF4FC0E, 0x6166FA91, 0x1A9E599A, 0xA3970A1F, 0xAB659687, 0xAFAB8D68,
		0xE74B1015, 0x34001A98, 0x4119EFF3, 0x930A0E76, 0x87B28070, 0x11EFE996};
	ethash_keccakf800(state);
	for (size_t i = 0; i < 25; ++i)
		BOOST_REQUIRE_EQUAL(state[i], expected_state_0[i]);
	const uint32_t expected_state_1[] = {0x75BF2D0D, 0x9B610E89, 0xC826AF40, 0x64CD84AB, 0xF905BDD6,
		0xBC832835, 0x5F8001B9, 0x15662CCE, 0x8E38C95E, 0x701FE543, 0x1B544380, 0x89ACDEFF,
		0x51EDB5DE, 0x0E9702D9, 0x6C19AA16, 0xA2913EEE, 0x60754E9A, 0x9819063C, 0xF4709254,
		0xD09F9084, 0x772DA259, 0x1DB35DF7, 0x5AA60162, 0x358825D5, 0xB3783BAB};
	ethash_keccakf800(state);
	for (size_t i = 0; i < 25; ++i)
		BOOST_REQUIRE_EQUAL(state[i], expected_state_1[i]);
}

BOOST_AUTO_TEST_CASE(test_progpow_block0_verification) {
	// epoch 0
	ethash_light_t light = ethash_light_new(1045);
	ethash_h256_t seedhash = stringToBlockhash("5fc898f16035bf5ac9c6d9077ae1e3d5fc1ecc3c9fd5bee8bb00e810fdacbaa0");
	BOOST_ASSERT(light);
	ethash_return_value_t ret = progpow_light_compute(
		light,
		seedhash,
		0x50377003e5d830caU,
		1045
	);
	//ethash_h256_t difficulty = ethash_h256_static_init(0x25, 0xa6, 0x1e);
	//BOOST_REQUIRE(ethash_check_difficulty(&ret.result, &difficulty));
	ethash_light_delete(light);
}

BOOST_AUTO_TEST_CASE(test_progpow_keccak_f800) {
	ethash_h256_t seedhash;
	ethash_h256_t headerhash = stringToBlockhash("0000000000000000000000000000000000000000000000000000000000000000");

	{
		const std::string
			seedexp = "5dd431e5fbc604f499bfa0232f45f8f142d0ff5178f539e5a7800bf0643697af";
		const std::string header_string = blockhashToHexString(&headerhash);
		BOOST_REQUIRE_MESSAGE(true,
				"\nheader: " << header_string.c_str() << "\n");
		uint32_t result[8];
		for (int i = 0; i < 8; i++)
			result[i] = 0;

		hash32_t header;
		memcpy((void *)&header, (void *)&headerhash, sizeof(headerhash));
		uint64_t nonce = 0x0;
		// keccak(header..nonce)
		uint64_t seed = keccak_f800(header, nonce, result);
		uint64_t exp = 0x5dd431e5fbc604f4U;

		BOOST_REQUIRE_MESSAGE(seed == exp,
				"\nseed: " << seed << "\n");
		ethash_h256_t out;
		memcpy((void *)&out, (void *)&result, sizeof(result));
		const std::string out_string = blockhashToHexString(&out);
		BOOST_REQUIRE_MESSAGE(out_string == seedexp,
				"\nresult: " << out_string.c_str() << "\n");
	}
}

BOOST_AUTO_TEST_CASE(test_progpow_full_client_checks) {
	uint64_t full_size = ethash_get_datasize(0);
	uint64_t cache_size = ethash_get_cachesize(0);
	ethash_h256_t difficulty;
	ethash_return_value_t light_out;
	ethash_return_value_t full_out;
	ethash_h256_t hash = stringToBlockhash("0000000000000000000000000000000000000000000000000000000000000000");
	ethash_h256_t seed = stringToBlockhash("0000000000000000000000000000000000000000000000000000000000000000");

	// Set the difficulty
	ethash_h256_set(&difficulty, 0, 197);
	ethash_h256_set(&difficulty, 1, 90);
	for (int i = 2; i < 32; i++)
		ethash_h256_set(&difficulty, i, 255);

	ethash_light_t light = ethash_light_new_internal(cache_size, &seed);
	ethash_full_t full = ethash_full_new_internal(
		"./test_ethash_directory/",
		seed,
		full_size,
		light,
		NULL
	);
	{
		uint64_t nonce = 0x0;
		full_out = progpow_full_compute(full, hash, nonce, 0);
		BOOST_REQUIRE(full_out.success);

		const std::string
			exphead = "7ea12cfc33f64616ab7dbbddf3362ee7dd3e1e20d60d860a85c51d6559c912c4",
			expmix = "a09ffaa0f2b5d47a98c2d4fbc0e90936710dd2b2a220fce04e8d55a6c6a093d6";
		const std::string seed_string = blockhashToHexString(&seed);
		const std::string hash_string = blockhashToHexString(&hash);

		const std::string full_mix_hash_string = blockhashToHexString(&full_out.mix_hash);
		BOOST_REQUIRE_MESSAGE(full_mix_hash_string == expmix,
				"\nfull mix hash: " << full_mix_hash_string.c_str() << "\n");
		const std::string full_result_string = blockhashToHexString(&full_out.result);
		BOOST_REQUIRE_MESSAGE(full_result_string == exphead,
				"\nfull result: " << full_result_string.c_str() << "\n");
	}

	ethash_light_delete(light);
	ethash_full_delete(full);
	//fs::remove_all("./test_ethash_directory/");
}

BOOST_AUTO_TEST_CASE(test_progpow_light_client_checks) {
	uint64_t full_size = ethash_get_datasize(0);
	uint64_t cache_size = ethash_get_cachesize(0);
	ethash_return_value_t light_out;
	ethash_h256_t hash = stringToBlockhash("0000000000000000000000000000000000000000000000000000000000000000");
	ethash_h256_t seed = stringToBlockhash("0000000000000000000000000000000000000000000000000000000000000000");
	ethash_light_t light = ethash_light_new_internal(cache_size, &seed);
	{
		uint64_t nonce = 0x0;
		const std::string
			exphead = "7ea12cfc33f64616ab7dbbddf3362ee7dd3e1e20d60d860a85c51d6559c912c4",
			expmix = "a09ffaa0f2b5d47a98c2d4fbc0e90936710dd2b2a220fce04e8d55a6c6a093d6";
		const std::string hash_string = blockhashToHexString(&hash);

		light_out = progpow_light_compute_internal(light, full_size, hash, nonce, 0);
		BOOST_REQUIRE(light_out.success);

		const std::string light_result_string = blockhashToHexString(&light_out.result);
		BOOST_REQUIRE_MESSAGE(exphead == light_result_string,
				"\nlight result: " << light_result_string.c_str() << "\n"
						<< "exp result: " << exphead.c_str() << "\n");
		const std::string light_mix_hash_string = blockhashToHexString(&light_out.mix_hash);
		BOOST_REQUIRE_MESSAGE(expmix == light_mix_hash_string,
				"\nlight mix hash: " << light_mix_hash_string.c_str() << "\n"
						<< "exp mix hash: " << expmix.c_str() << "\n");
	}

	ethash_light_delete(light);
}

/// Defines a test case for ProgPoW hash() function. (from chfast/ethash/test/unittests/progpow_test_vectors.hpp)
struct progpow_hash_test_case
{
	int block_number;
	const char* header_hash_hex;
	const char* nonce_hex;
	const char* mix_hash_hex;
	const char* final_hash_hex;
};

progpow_hash_test_case progpow_hash_test_cases[] = {
	{0, "0000000000000000000000000000000000000000000000000000000000000000", "0000000000000000",
		"a09ffaa0f2b5d47a98c2d4fbc0e90936710dd2b2a220fce04e8d55a6c6a093d6",
		"7ea12cfc33f64616ab7dbbddf3362ee7dd3e1e20d60d860a85c51d6559c912c4"},
	{49, "7ea12cfc33f64616ab7dbbddf3362ee7dd3e1e20d60d860a85c51d6559c912c4", "0000000006ff2c47",
		"4e453d59426905122ef3d176a6fe660f29b53fdf2f82b5af2753dbaaebebf609",
		"f0167e445f8510504ce024856ec614a1a4461610bf58caa32df731ee4c315641"},
	{50, "f0167e445f8510504ce024856ec614a1a4461610bf58caa32df731ee4c315641", "00000000076e482e",
		"4e5291ae6132f64bff00dd05861721b0da701f789e7e65d096b9affa24bffd7e",
		"fdc3bce3e0d0b1a5af43f84acc7d5421d423ec5d3b7e41698178b24c459a6cbe"},
	{99, "fdc3bce3e0d0b1a5af43f84acc7d5421d423ec5d3b7e41698178b24c459a6cbe", "000000003917afab",
		"d35c7e4012204d1db243dc7cf0bf2075f897e362e6ad2b36c02e325cfc6f8dbb",
		"5b014c2c706476b56cf3b9c37ed999d30b20c0fb038d27cc94c991dacef62033"},
	{29950, "5b014c2c706476b56cf3b9c37ed999d30b20c0fb038d27cc94c991dacef62033", "005d409dbc23a62a",
		"0c64704dedb0677149b47fabc6726e9ff0585233692c8562e485a330ce90c0e9",
		"a01b432e82cacaae095ef402b575f1764c45247ba9cf17e99d5432cf00829ee2"},
	{29999, "a01b432e82cacaae095ef402b575f1764c45247ba9cf17e99d5432cf00829ee2", "005db5fa4c2a3d03",
		"3d95cad9cf4513bb31a4766d3a2f488bbff1baa57da8b2252e246ac91594c769",
		"0fc3e6e1392033619f614ec3236d8fbfcefe94d9fdc341a4d7daeffa0b8ad35d"},
	{30000, "0fc3e6e1392033619f614ec3236d8fbfcefe94d9fdc341a4d7daeffa0b8ad35d", "005db8607994ff30",
		"7ee9d0c571ed35073404454eebe9a73a6d677a32446cf6c427ee63a63bd512da",
		"b94de4495555dc2ab4ad8725cabd395178813c8c434134b2f25062b5f72dafb9"},
	{30049, "b94de4495555dc2ab4ad8725cabd395178813c8c434134b2f25062b5f72dafb9", "005e2e215a8ca2e7",
		"7a16d37208288152237afdc13724d26fe7aadf3cd354a42c587a4192761ef18e",
		"e152d3770855cea35a94ee53ab321f93ee3a426513c6ab1ec5e8d81ea9a661d7"},
	{30050, "e152d3770855cea35a94ee53ab321f93ee3a426513c6ab1ec5e8d81ea9a661d7", "005e30899481055e",
		"005df2434f2a5265c2ed0d13dd12308795620202d2784a40967461c383f859a3",
		"55d013e85571e46e914a7529909fbfc686965a92c7baaef2e89e5b5f533a6dc9"},
	{30099, "55d013e85571e46e914a7529909fbfc686965a92c7baaef2e89e5b5f533a6dc9", "005ea6aef136f88b",
		"d8b1046cc2c8273a06e6f7ce19b7b4aefb7fb43b141721663252e2872b654548",
		"8ba5629b6affa0514c2f4951c3a63761465ef0e5be7cbb8f9ce230a5564faccb"},
	{59950, "8ba5629b6affa0514c2f4951c3a63761465ef0e5be7cbb8f9ce230a5564faccb", "02ebe0503bd7b1da",
		"b3131de1a747449e5328f50742447d5c6da637a5d141a117caf9a986bd524de9",
		"10af438404304f4a7de0b07e7d08bfc80b521860237e3e2d47f77630eef5f742"},
	{59999, "10af438404304f4a7de0b07e7d08bfc80b521860237e3e2d47f77630eef5f742", "02edb6275bd221e3",
		"87f7d6c73fb86a5ed00d2ad7fff7b2a8a9796c3138b31f2473b89065946cb0ed",
		"3863e5c767a6b0d28f5cf1d261e35c52fe03f7fd690d50c10596ec73d7595887"},
};

BOOST_AUTO_TEST_CASE(test_progpow_test_cases) {
	ethash_light_t light;
	uint32_t epoch = -1;
	for (int i = 0; i < sizeof(progpow_hash_test_cases) / sizeof(progpow_hash_test_case); i++)
	{
		progpow_hash_test_case *t;
		t = &progpow_hash_test_cases[i];
		const auto epoch_number = t->block_number / ETHASH_EPOCH_LENGTH;
		if (!light || epoch != epoch_number)
			light = ethash_light_new(t->block_number);
		epoch = epoch_number;
		ethash_h256_t hash = stringToBlockhash(t->header_hash_hex);
		uint64_t nonce = strtoul(t->nonce_hex, NULL, 16);
		ethash_return_value_t light_out = progpow_light_compute(light, hash, nonce, t->block_number);
		BOOST_REQUIRE_EQUAL(blockhashToHexString(&light_out.result), t->final_hash_hex);
		BOOST_REQUIRE_EQUAL(blockhashToHexString(&light_out.mix_hash), t->mix_hash_hex);
		printf("next...\n");
	}
	ethash_light_delete(light);
}
