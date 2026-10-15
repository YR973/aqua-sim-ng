/*
 * CB-ERGR v5 test suite (spec section 14).
 *
 * Tests are added milestone by milestone. M0 only checks that the module
 * links against Aqua-Sim NG and that the upstream types the spec relies on
 * are registered.
 */

#include "ns3/cb-ergr-config.h"
#include "ns3/test.h"
#include "ns3/type-id.h"

using namespace ns3;

/**
 * M0: the Aqua-Sim NG classes named in the spec exist under these names.
 */
class CbErgrUpstreamTypesTestCase : public TestCase
{
  public:
    CbErgrUpstreamTypesTestCase()
        : TestCase("M0: upstream Aqua-Sim NG types are registered")
    {
    }

  private:
    void DoRun() override
    {
        const char* names[] = {"ns3::AquaSimPhyCmn",
                               "ns3::AquaSimEnergyModel",
                               "ns3::AquaSimRouting",
                               "ns3::AquaSimChannel",
                               "ns3::AquaSimRangePropagation",
                               "ns3::AquaSimBroadcastMac",
                               "ns3::AquaSimAloha"};
        for (const char* n : names)
        {
            TypeId tid;
            NS_TEST_ASSERT_MSG_EQ(TypeId::LookupByNameFailSafe(n, &tid), true, "missing type " << n);
        }
        NS_TEST_ASSERT_MSG_EQ(cbergr::kFeatureDim, 9, "feature dimension");
    }
};

class CbErgrTestSuite : public TestSuite
{
  public:
    CbErgrTestSuite()
        : TestSuite("cb-ergr", UNIT)
    {
        AddTestCase(new CbErgrUpstreamTypesTestCase, TestCase::QUICK);
    }
};

static CbErgrTestSuite g_cbErgrTestSuite;
