/*

 Copyright (c) 2015, Hookflash Inc. / Hookflash Inc.
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

#include <ortc/internal/types.h>

#include <ortc/IICETransport.h>
#include <ortc/IRTPReceiver.h>
#include <ortc/IDTLSTransport.h>
#include <ortc/IMediaStreamTrack.h>

#include <openpeer/services/IWakeDelegate.h>
#include <zsLib/MessageQueueAssociator.h>
#include <zsLib/Timer.h>


//#define ORTC_SETTING_SCTP_TRANSPORT_MAX_MESSAGE_SIZE "ortc/sctp/max-message-size"

namespace ortc
{
  namespace internal
  {
    ZS_DECLARE_INTERACTION_PTR(IRTPReceiverForSettings)
    ZS_DECLARE_INTERACTION_PTR(IRTPReceiverForRTPListener)
    ZS_DECLARE_INTERACTION_PTR(IRTPReceiverChannelForRTPReceiver)

    ZS_DECLARE_INTERACTION_PTR(ISecureTransportForRTPReceiver)
    ZS_DECLARE_INTERACTION_PTR(IRTPListenerForRTPReceiver)
    ZS_DECLARE_INTERACTION_PTR(IRTPReceiverForRTPReceiverChannel)
    ZS_DECLARE_INTERACTION_PTR(IMediaStreamTrackForRTPReceiver)

    ZS_DECLARE_INTERACTION_PROXY(IRTPReceiverAsyncDelegate)

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPReceiverForSettings
    #pragma mark

    interaction IRTPReceiverForSettings
    {
      ZS_DECLARE_TYPEDEF_PTR(IRTPReceiverForSettings, ForSettings)

      static void applyDefaults();

      virtual ~IRTPReceiverForSettings() {}
    };
    
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPReceiverForRTPListener
    #pragma mark

    interaction IRTPReceiverForRTPListener
    {
      ZS_DECLARE_TYPEDEF_PTR(IRTPReceiverForRTPListener, ForRTPListener)

      static ElementPtr toDebug(ForRTPListenerPtr transport);

      virtual PUID getID() const = 0;

      virtual bool handlePacket(
                                IICETypes::Components viaTransport,
                                RTPPacketPtr packet
                                ) = 0;

      virtual bool handlePacket(
                                IICETypes::Components viaTransport,
                                RTCPPacketPtr packet
                                ) = 0;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPReceiverForRTPReceiverChannel
    #pragma mark

    interaction IRTPReceiverForRTPReceiverChannel
    {
      ZS_DECLARE_TYPEDEF_PTR(IRTPReceiverForRTPReceiverChannel, ForRTPReceiverChannel)

      static ElementPtr toDebug(ForRTPReceiverChannelPtr object);

      virtual PUID getID() const = 0;

      virtual bool sendPacket(RTCPPacketPtr packet) = 0;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPReceiverAsyncDelegate
    #pragma mark

    interaction IRTPReceiverAsyncDelegate
    {
      virtual ~IRTPReceiverAsyncDelegate() {}
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPReceiver
    #pragma mark
    
    class RTPReceiver : public Noop,
                        public MessageQueueAssociator,
                        public SharedRecursiveLock,
                        public IRTPReceiver,
                        public IRTPReceiverForSettings,
                        public IRTPReceiverForRTPListener,
                        public IRTPReceiverForRTPReceiverChannel,
                        public IWakeDelegate,
                        public zsLib::ITimerDelegate,
                        public IRTPReceiverAsyncDelegate
    {
    protected:
      struct make_private {};

    public:
      friend interaction IRTPReceiver;
      friend interaction IRTPReceiverFactory;
      friend interaction IRTPReceiverForSettings;
      friend interaction IRTPReceiverForRTPListener;
      friend interaction IRTPReceiverForRTPReceiverChannel;

      ZS_DECLARE_TYPEDEF_PTR(ISecureTransportForRTPReceiver, UseSecureTransport)
      ZS_DECLARE_TYPEDEF_PTR(IRTPListenerForRTPReceiver, UseListener)
      ZS_DECLARE_TYPEDEF_PTR(IRTPReceiverChannelForRTPReceiver, UseChannel)
      ZS_DECLARE_TYPEDEF_PTR(IMediaStreamTrackForRTPReceiver, UseMediaStreamTrack)

      enum States
      {
        State_Pending,
        State_Ready,
        State_ShuttingDown,
        State_Shutdown,
      };
      static const char *toString(States state);


    public:
      RTPReceiver(
                  const make_private &,
                  IMessageQueuePtr queue,
                  IRTPReceiverDelegatePtr delegate,
                  IRTPTransportPtr transport,
                  IRTCPTransportPtr rtcpTransport = IRTCPTransportPtr()
                  );

    protected:
      RTPReceiver(Noop) :
        Noop(true),
        MessageQueueAssociator(IMessageQueuePtr()),
        SharedRecursiveLock(SharedRecursiveLock::create())
      {}

      void init();

    public:
      virtual ~RTPReceiver();

      static RTPReceiverPtr convert(IRTPReceiverPtr object);
      static RTPReceiverPtr convert(ForSettingsPtr object);
      static RTPReceiverPtr convert(ForRTPListenerPtr object);

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => IStatsProvider
      #pragma mark

      virtual PromiseWithStatsReportPtr getStats() const throw(InvalidStateError) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => IRTPReceiver
      #pragma mark

      static ElementPtr toDebug(RTPReceiverPtr receiver);

      static RTPReceiverPtr create(
                                   IRTPReceiverDelegatePtr delegate,
                                   IRTPTransportPtr transport,
                                   IRTCPTransportPtr rtcpTransport = IRTCPTransportPtr()
                                   );

      virtual PUID getID() const override {return mID;}

      virtual IRTPReceiverSubscriptionPtr subscribe(IRTPReceiverDelegatePtr delegate) override;

      virtual IMediaStreamTrackPtr track() const override;
      virtual IRTPTransportPtr transport() const override;
      virtual IRTCPTransportPtr rtcpTransport() const override;

      virtual void setTransport(
                                IRTPTransportPtr transport,
                                IRTCPTransportPtr rtcpTransport = IRTCPTransportPtr()
                                ) override;

      static CapabilitiesPtr getCapabilities(Optional<Kinds> kind);

      virtual void receive(const Parameters &parameters) override;
      virtual void stop() override;

      virtual ContributingSourceList getContributingSources() const override;

      virtual void requestSendCSRC(SSRCType csrc) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => IRTPReceiverForRTPListener
      #pragma mark

      // (duplciate) static ElementPtr toDebug(ForRTPListenerPtr transport);

      // (duplicate) virtual PUID getID() const = 0;

      virtual bool handlePacket(
                                IICETypes::Components viaTransport,
                                RTPPacketPtr packet
                                ) override;

      virtual bool handlePacket(
                                IICETypes::Components viaTransport,
                                RTCPPacketPtr packet
                                ) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => IRTPReceiverForRTPReceiverChannel
      #pragma mark

      // (duplicate) static ElementPtr toDebug(ForRTPReceiverChannelPtr object);

      // (duplicate) virtual PUID getID() const = 0;

      virtual bool sendPacket(RTCPPacketPtr packet) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => IWakeDelegate
      #pragma mark

      virtual void onWake() override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => ITimerDelegate
      #pragma mark

      virtual void onTimer(TimerPtr timer) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => IRTPReceiverAsyncDelegate
      #pragma mark

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => (internal)
      #pragma mark

      Log::Params log(const char *message) const;
      Log::Params debug(const char *message) const;
      virtual ElementPtr toDebug() const;

      bool isShuttingDown() const;
      bool isShutdown() const;

      void step();
      bool stepBogusDoSomething();

      void cancel();

      void setState(States state);
      void setError(WORD error, const char *reason = NULL);

      bool sendPacket(
                      IICETypes::Components packetType,
                      const BYTE *buffer,
                      size_t bufferSizeInBytes
                      );

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark RTPReceiver => (data)
      #pragma mark

      AutoPUID mID;
      RTPReceiverWeakPtr mThisWeak;
      RTPReceiverPtr mGracefulShutdownReference;

      IRTPReceiverDelegateSubscriptions mSubscriptions;
      IRTPReceiverSubscriptionPtr mDefaultSubscription;

      States mCurrentState {State_Pending};

      WORD mLastError {};
      String mLastErrorReason;

      UseMediaStreamTrackPtr mVideoTrack;

      ParametersPtr mParameters;
      
      UseListenerPtr mListener;

      UseSecureTransportPtr mRTPTransport;
      UseSecureTransportPtr mRTCPTransport;

      IICETypes::Components mReceiveRTPOverTransport {IICETypes::Component_RTP};
      IICETypes::Components mReceiveRTCPOverTransport {IICETypes::Component_RTCP};
      IICETypes::Components mSendRTCPOverTransport {IICETypes::Component_RTCP};
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPReceiverFactory
    #pragma mark

    interaction IRTPReceiverFactory
    {
      typedef IRTPReceiverTypes::Kinds Kinds;
      typedef IRTPReceiverTypes::CapabilitiesPtr CapabilitiesPtr;

      static IRTPReceiverFactory &singleton();

      virtual RTPReceiverPtr create(
                                    IRTPReceiverDelegatePtr delegate,
                                    IRTPTransportPtr transport,
                                    IRTCPTransportPtr rtcpTransport = IRTCPTransportPtr()
                                    );

      virtual CapabilitiesPtr getCapabilities(Optional<Kinds> kind);
    };

    class RTPReceiverFactory : public IFactory<IRTPReceiverFactory> {};
  }
}

ZS_DECLARE_PROXY_BEGIN(ortc::internal::IRTPReceiverAsyncDelegate)
//ZS_DECLARE_PROXY_METHOD_0(onWhatever)
ZS_DECLARE_PROXY_END()
