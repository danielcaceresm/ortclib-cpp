/*

 Copyright (c) 2014, Hookflash Inc. / Hookflash Inc.
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The views and conclusions contained in the software and documentation are those
 of the authors and should not be interpreted as representing official policies,
 either expressed or implied, of the FreeBSD Project.
 
 */

#pragma once

#include <ortc/types.h>
#include <ortc/IICETypes.h>
#include <ortc/IStatsProvider.h>

namespace ortc
{
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  #pragma mark
  #pragma mark IICEGathererTypes
  #pragma mark
  
  interaction IICEGathererTypes : public IICETypes
  {
    ZS_DECLARE_STRUCT_PTR(Options)
    ZS_DECLARE_STRUCT_PTR(Server)
    ZS_DECLARE_STRUCT_PTR(InterfacePolicy)

    ZS_DECLARE_TYPEDEF_PTR(std::list<String>, StringList)
    ZS_DECLARE_TYPEDEF_PTR(std::list<Server>, ServerList)
    ZS_DECLARE_TYPEDEF_PTR(std::list<InterfacePolicy>, InterfacePolicyList)

    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IICEGathererTypes::States
    #pragma mark

    enum States {
      State_New,
      State_Gathering,
      State_Complete,
    };

    static const char *toString(States state);
    static States toState(const char *state);

    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IICEGathererTypes::FilterPolicies
    #pragma mark

    enum FilterPolicies {
      FilterPolicy_None                = 0,
      FilterPolicy_NoIPv4Host          = 0x00000001,
      FilterPolicy_NoIPv4Srflx         = 0x00000002,
      FilterPolicy_NoIPv4Prflx         = 0x00000004,
      FilterPolicy_NoIPv4Relay         = 0x00000008,
      FilterPolicy_NoIPv4              = 0x000000FF,
      FilterPolicy_NoIPv6Host          = 0x00000100,
      FilterPolicy_NoIPv6Srflx         = 0x00000200,
      FilterPolicy_NoIPv6Prflx         = 0x00000400,
      FilterPolicy_NoIPv6Relay         = 0x00000800,
      FilterPolicy_NoIPv6Tunnel        = 0x00001000,
      FilterPolicy_NoIPv6Permanent     = 0x00002000,
      FilterPolicy_NoIPv6              = 0x0000FF00,
      FilterPolicy_NoHost              = (FilterPolicy_NoIPv4Host | FilterPolicy_NoIPv6Host),
      FilterPolicy_NoSrflx             = (FilterPolicy_NoIPv4Srflx | FilterPolicy_NoIPv4Srflx),
      FilterPolicy_NoPrflx             = (FilterPolicy_NoIPv4Prflx | FilterPolicy_NoIPv6Prflx),
      FilterPolicy_NoRelay             = (FilterPolicy_NoIPv4Relay | FilterPolicy_NoIPv6Relay),
      FilterPolicy_RelayOnly           = (FilterPolicy_NoIPv4Host | FilterPolicy_NoSrflx | FilterPolicy_NoPrflx),
      FilterPolicy_NoCandidates        = (0xFFFFFFFF)
    };

    static String toString(FilterPolicies policies);
    static FilterPolicies toPolicy(const char *filters);

    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IICEGathererTypes::Options
    #pragma mark

    struct Options {
      bool                mContinuousGathering {true};
      FilterPolicies      mGatherPolicy {FilterPolicy_None};
      InterfacePolicyList mInterfacePolicy;
      ServerList          mICEServers;
    };

    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IICEGathererTypes::Server
    #pragma mark

    struct Server {
      StringList mURLs;
      String     mUserName;
      String     mCredential;
    };

    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IICEGathererTypes::InterfacePolicy
    #pragma mark

    struct InterfacePolicy {
      String          mInterfaceType;
      FilterPolicies  mGatherPolicy {FilterPolicy_None};
    };
  };

  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  #pragma mark
  #pragma mark IICEGatherer
  #pragma mark
  
  interaction IICEGatherer : public IICEGathererTypes,
                             public IStatsProvider
  {
    static ElementPtr toDebug(IICETransportPtr gatherer);

    static IICEGathererPtr create(
                                  IICEGathererDelegatePtr delegate,
                                  Options options
                                  );

    virtual PUID getID() const = 0;

    virtual IICEGathererSubscriptionPtr subscribe(IICEGathererDelegatePtr delegate) = 0;

    virtual States state() const = 0;

    virtual ParametersPtr getLocalParameters() const = 0;
    virtual ParametersPtr getRemoteParameters() const = 0;
  };

  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  #pragma mark
  #pragma mark IICEGathererDelegate
  #pragma mark

  interaction IICEGathererDelegate
  {
    ZS_DECLARE_TYPEDEF_PTR(IICETypes::Candidate, Candidate)
    typedef WORD ErrorCode;

    virtual void onICEGathererStateChanged(
                                           IICEGathererPtr gatherer,
                                           IICEGatherer::States state
                                           ) = 0;

    virtual void onICEGathererLocalCandidate(
                                             IICEGathererPtr gatherer,
                                             CandidatePtr candidate
                                             ) = 0;
    virtual void onICEGathererError(
                                     IICEGathererPtr gatherer,
                                     ErrorCode errorCode,
                                     String errorReason
                                     ) = 0;
  };

  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------
  #pragma mark
  #pragma mark IICEGathererSubscription
  #pragma mark

  interaction IICEGathererSubscription
  {
    virtual PUID getID() const = 0;

    virtual void cancel() = 0;

    virtual void background() = 0;
  };
}

ZS_DECLARE_PROXY_BEGIN(ortc::IICEGathererDelegate)
ZS_DECLARE_PROXY_TYPEDEF(ortc::IICEGathererPtr, IICEGathererPtr)
ZS_DECLARE_PROXY_TYPEDEF(ortc::IICEGatherer::States, States)
ZS_DECLARE_PROXY_TYPEDEF(ortc::IICEGathererDelegate::CandidatePtr, CandidatePtr)
ZS_DECLARE_PROXY_TYPEDEF(ortc::IICEGathererDelegate::ErrorCode, ErrorCode)
ZS_DECLARE_PROXY_METHOD_2(onICEGathererStateChanged, IICEGathererPtr, States)
ZS_DECLARE_PROXY_METHOD_2(onICEGathererLocalCandidate, IICEGathererPtr, CandidatePtr)
ZS_DECLARE_PROXY_METHOD_3(onICEGathererError, IICEGathererPtr, ErrorCode, String)
ZS_DECLARE_PROXY_END()

ZS_DECLARE_PROXY_SUBSCRIPTIONS_BEGIN(ortc::IICEGathererDelegate, ortc::IICEGathererSubscription)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_TYPEDEF(ortc::IICEGathererPtr, IICEGathererPtr)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_TYPEDEF(ortc::IICEGatherer::States, States)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_TYPEDEF(ortc::IICEGathererDelegate::CandidatePtr, CandidatePtr)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_TYPEDEF(ortc::IICEGathererDelegate::ErrorCode, ErrorCode)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_METHOD_2(onICEGathererStateChanged, IICEGathererPtr, States)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_METHOD_2(onICEGathererLocalCandidate, IICEGathererPtr, CandidatePtr)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_METHOD_3(onICEGathererError, IICEGathererPtr, ErrorCode, String)
ZS_DECLARE_PROXY_SUBSCRIPTIONS_END()
