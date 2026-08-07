#include <gtest/gtest.h>

#include <net/NetAddress.h>

#include <unordered_map>

TEST(NetAddress, ParsesIPv4WithPort)
{
    const auto address = NetAddressFromString("127.0.0.1:27500");

    ASSERT_TRUE(address.has_value());
    EXPECT_EQ(address->Family, NetAddressFamily::IPv4);
    EXPECT_EQ(address->Port, 27500);
    EXPECT_EQ(address->Bytes[0], 127);
    EXPECT_EQ(address->Bytes[1], 0);
    EXPECT_EQ(address->Bytes[2], 0);
    EXPECT_EQ(address->Bytes[3], 1);
}

TEST(NetAddress, ParsesBracketedIPv6WithPort)
{
    const auto address = NetAddressFromString("[::1]:27500");

    ASSERT_TRUE(address.has_value());
    EXPECT_EQ(address->Family, NetAddressFamily::IPv6);
    EXPECT_EQ(address->Port, 27500);
    EXPECT_EQ(address->Bytes[15], 1);
    for (std::size_t i = 0; i < 15; ++i)
        EXPECT_EQ(address->Bytes[i], 0) << "byte " << i;
}

TEST(NetAddress, ParsesFullIPv6)
{
    const auto address =
        NetAddressFromString("[2001:0db8:0000:0000:0000:ff00:0042:8329]:1234");

    ASSERT_TRUE(address.has_value());
    EXPECT_EQ(address->Bytes[0], 0x20);
    EXPECT_EQ(address->Bytes[1], 0x01);
    EXPECT_EQ(address->Bytes[2], 0x0d);
    EXPECT_EQ(address->Bytes[3], 0xb8);
    EXPECT_EQ(address->Bytes[11], 0x00);
    EXPECT_EQ(address->Bytes[12], 0x00);
    EXPECT_EQ(address->Bytes[13], 0x42);
    EXPECT_EQ(address->Bytes[14], 0x83);
    EXPECT_EQ(address->Bytes[15], 0x29);
}

TEST(NetAddress, RoundTripsThroughText)
{
    for (std::string_view text : { "127.0.0.1:27500", "10.0.0.7:1", "192.168.1.255:65535" })
    {
        const auto parsed = NetAddressFromString(text);
        ASSERT_TRUE(parsed.has_value()) << text;
        EXPECT_EQ(NetAddressToString(*parsed), text);
    }
}

// Every one of these is a shape that a permissive parser would accept and then
// disagree with some other parser about. An address is a security boundary --
// it is what per-peer budgets, strikes, and bans key on -- so an ambiguous
// spelling has to be refused rather than normalized.
TEST(NetAddress, RefusesMalformedText)
{
    for (std::string_view text : {
             "",
             "127.0.0.1",           // no port
             "127.0.0.1:",          // empty port
             "127.0.0.1:70000",     // port out of range
             "127.0.0.1:-1",
             "256.0.0.1:80",        // octet out of range
             "127.0.0.01:80",       // leading zero reads differently elsewhere
             "127.0.0:80",          // too few octets
             "127.0.0.1.5:80",      // too many octets
             "::1:80",              // IPv6 needs brackets to disambiguate
             "[::1]80",             // missing port separator
             "[::1]:",
             "[gggg::1]:80",
             "[1:2:3:4:5:6:7:8:9]:80",
             "[::1::2]:80",         // two elisions
         })
    {
        EXPECT_FALSE(NetAddressFromString(text).has_value()) << "accepted " << text;
    }
}

TEST(NetAddress, LoopbackMatchesItsParsedForm)
{
    EXPECT_EQ(NetAddressLoopback(27500), NetAddressFromString("127.0.0.1:27500"));
    EXPECT_EQ(NetAddressLoopback(27500, NetAddressFamily::IPv6),
              NetAddressFromString("[::1]:27500"));
}

// Several clients behind one NAT share an address and differ only in port, so
// the port has to be part of identity or they collapse into one peer.
TEST(NetAddress, PortIsPartOfIdentity)
{
    const NetAddress first = NetAddressLoopback(1000);
    const NetAddress second = NetAddressLoopback(1001);

    EXPECT_NE(first, second);

    std::unordered_map<NetAddress, int> peers;
    peers[first] = 1;
    peers[second] = 2;
    EXPECT_EQ(peers.size(), 2u);
    EXPECT_EQ(peers[first], 1);
    EXPECT_EQ(peers[second], 2);
}

TEST(NetAddress, FamilyIsPartOfIdentity)
{
    EXPECT_NE(NetAddressLoopback(80), NetAddressLoopback(80, NetAddressFamily::IPv6));
}

TEST(NetAddress, DefaultIsInvalid)
{
    EXPECT_FALSE(NetAddress{}.IsValid());
    EXPECT_EQ(NetAddressToString(NetAddress{}), "<unspecified>");
}
