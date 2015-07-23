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
#include <ortc/internal/ortc_SCTPTransport.h>

#include <ortc/IDataChannel.h>

#include <openpeer/services/IWakeDelegate.h>

#include <zsLib/MessageQueueAssociator.h>
#include <zsLib/Timer.h>

//#define ORTC_SETTING_SRTP_TRANSPORT_WARN_OF_KEY_LIFETIME_EXHAUGSTION_WHEN_REACH_PERCENTAGE_USSED "ortc/srtp/warm-key-lifetime-exhaustion-when-reach-percentage-used"

namespace ortc
{
  namespace internal
  {
    ZS_DECLARE_INTERACTION_PTR(ISCTPTransportForDataChannel)

    ZS_DECLARE_INTERACTION_PTR(IDataChannelForSettings)
    ZS_DECLARE_INTERACTION_PTR(IDataChannelForSCTPTransport)

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IDataChannelForSettings
    #pragma mark

    interaction IDataChannelForSettings
    {
      ZS_DECLARE_TYPEDEF_PTR(IDataChannelForSettings, ForSettings)

      static void applyDefaults();

      virtual ~IDataChannelForSettings() {}
    };
    
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IDataChannelForSCTPTransport
    #pragma mark

    interaction IDataChannelForSCTPTransport
    {
      ZS_DECLARE_TYPEDEF_PTR(IDataChannelForSCTPTransport, ForDataTransport)

      static ElementPtr toDebug(ForDataTransportPtr transport);

      virtual PUID getID() const = 0;

      virtual bool notifySendSCTPPacket(
                                        const BYTE *buffer,
                                        size_t bufferLengthInBytes
                                        ) = 0;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark DataChannel
    #pragma mark
    
    class DataChannel : public Noop,
                        public MessageQueueAssociator,
                        public SharedRecursiveLock,
                        public IDataChannel,
                        public IDataChannelForSettings,
                        public IDataChannelForSCTPTransport,
                        public ISCTPTransportForDataChannelDelegate,
                        public IWakeDelegate,
                        public zsLib::ITimerDelegate
    {
    protected:
      struct make_private {};

    public:
      friend interaction IDataChannel;
      friend interaction IDataChannelFactory;
      friend interaction IDataChannelForSettings;
      friend interaction IDataChannelForSCTPTransport;

      ZS_DECLARE_TYPEDEF_PTR(ISCTPTransportForDataChannel, UseDataTransport)

    public:
      DataChannel(
                  const make_private &,
                  IMessageQueuePtr queue,
                  IDataChannelDelegatePtr delegate,
                  IDataTransportPtr transport
                  );

    protected:
      DataChannel(Noop) :
        Noop(true),
        MessageQueueAssociator(IMessageQueuePtr()),
        SharedRecursiveLock(SharedRecursiveLock::create())
      {}

      void init();

    public:
      virtual ~DataChannel();

      static DataChannelPtr convert(IDataChannelPtr object);
      static DataChannelPtr convert(ForSettingsPtr object);
      static DataChannelPtr convert(ForDataTransportPtr object);

    protected:

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => IDataChannel
      #pragma mark

      static ElementPtr toDebug(DataChannelPtr transport);

      static DataChannelPtr create(
                                   IDataChannelDelegatePtr delegate,
                                   IDataTransportPtr transport
                                   );

      virtual PUID getID() const override {return mID;}

      virtual IDataChannelSubscriptionPtr subscribe(IDataChannelDelegatePtr delegate) override;

      virtual IDataTransportPtr transport() const override;

      virtual ParametersPtr parameters() const override;

      virtual States readyState() const override;

      virtual ULONG bufferedAmount() const override;

      virtual String binaryType() const override;

      virtual void close() override;

      virtual void send(const String &data) override;
      virtual void send(const SecureByteBlock &data) override;
      virtual void send(
                        const BYTE *buffer,
                        size_t bufferSizeInBytes
                        ) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => IDataChannelForSCTPTransport
      #pragma mark

      // (duplicate) static ElementPtr toDebug(DataChannelPtr transport);

      // (duplicate) virtual PUID getID() const = 0;

      virtual bool notifySendSCTPPacket(
                                        const BYTE *buffer,
                                        size_t bufferLengthInBytes
                                        ) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => ISCTPTransportForDataChannelDelegate
      #pragma mark
      
      virtual void onSCTPTransportReady() override;
      virtual void onSCTPTransportClosed() override;

      
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => IWakeDelegate
      #pragma mark

      virtual void onWake() override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => ITimerDelegate
      #pragma mark

      virtual void onTimer(TimerPtr timer) override;

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => IDataChannelAsyncDelegate
      #pragma mark


    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => (internal)
      #pragma mark

      Log::Params log(const char *message) const;
      static Log::Params slog(const char *message);
      Log::Params debug(const char *message) const;
      virtual ElementPtr toDebug() const;

      bool isShuttingDown() const;
      bool isShutdown() const;

      void step();

      bool stepBogusDoSomething();

      void cancel();

      void setState(States state);
      void setError(WORD error, const char *reason = NULL);

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark DataChannel => (data)
      #pragma mark

      AutoPUID mID;
      DataChannelWeakPtr mThisWeak;
      DataChannelPtr mGracefulShutdownReference;

      IDataChannelDelegateSubscriptions mSubscriptions;
      IDataChannelSubscriptionPtr mDefaultSubscription;

      UseDataTransportPtr mDataTransport;

      States mCurrentState {State_Connecting};

      WORD mLastError {};
      String mLastErrorReason;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IDataChannelFactory
    #pragma mark

    interaction IDataChannelFactory
    {
      static IDataChannelFactory &singleton();

      virtual DataChannelPtr create(
                                    IDataChannelDelegatePtr delegate,
                                    IDataTransportPtr transport
                                    );
    };

    class DataChannelFactory : public IFactory<IDataChannelFactory> {};
  }
}
