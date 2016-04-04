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

#include <ortc/internal/ortc_RTPMediaEngine.h>
#include <ortc/internal/ortc_MediaStreamTrack.h>
#include <ortc/internal/ortc_RTPReceiverChannelAudio.h>
#include <ortc/internal/ortc_RTPReceiverChannelVideo.h>
#include <ortc/internal/ortc_RTPSenderChannelAudio.h>
#include <ortc/internal/ortc_RTPSenderChannelVideo.h>
#include <ortc/internal/ortc_RTPPacket.h>
#include <ortc/internal/ortc_RTCPPacket.h>
#include <ortc/internal/ortc_ORTC.h>
#include <ortc/internal/ortc_Tracing.h>
#include <ortc/internal/platform.h>

#include <openpeer/services/ISettings.h>
#include <openpeer/services/IHelper.h>
#include <openpeer/services/IHTTP.h>

#include <zsLib/Stringize.h>
#include <zsLib/Singleton.h>
#include <zsLib/Log.h>
#include <zsLib/XML.h>

#include <cryptopp/sha.h>

#include <webrtc/voice_engine/include/voe_codec.h>
#include <webrtc/voice_engine/include/voe_rtp_rtcp.h>
#include <webrtc/voice_engine/include/voe_network.h>
#include <webrtc/system_wrappers/include/cpu_info.h>


#ifdef _DEBUG
#define ASSERT(x) ZS_THROW_BAD_STATE_IF(!(x))
#else
#define ASSERT(x)
#endif //_DEBUG


namespace ortc { ZS_DECLARE_SUBSYSTEM(ortclib) }

namespace ortc
{
  ZS_DECLARE_TYPEDEF_PTR(openpeer::services::ISettings, UseSettings)
  ZS_DECLARE_TYPEDEF_PTR(openpeer::services::IHelper, UseServicesHelper)
  ZS_DECLARE_TYPEDEF_PTR(openpeer::services::IHTTP, UseHTTP)

  typedef openpeer::services::Hasher<CryptoPP::SHA1> SHA1Hasher;

  namespace internal
  {
    ZS_DECLARE_CLASS_PTR(RTPMediaEngineRegistration)
    ZS_DECLARE_CLASS_PTR(RTPMediaEngineSingleton)

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark (helpers)
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngineRegistration
    #pragma mark

    class RTPMediaEngineRegistration : public IRTPMediaEngineRegistration
    {
    protected:
      struct make_private {};
    public:
      //-----------------------------------------------------------------------
      RTPMediaEngineRegistration(const make_private &)
      {}

      //-----------------------------------------------------------------------
      ~RTPMediaEngineRegistration()
      {
        mEngine->shutdown();
        mEngine.reset();
      }

      //-----------------------------------------------------------------------
      static RTPMediaEngineRegistrationPtr create()
      {
        RTPMediaEngineRegistrationPtr pThis(make_shared<RTPMediaEngineRegistration>(make_private{}));
        pThis->mThisWeak = pThis;
        pThis->init();
        return pThis;
      }

      //-----------------------------------------------------------------------
      void init()
      {
        mEngine = IRTPMediaEngineFactory::singleton().create(mThisWeak.lock());
      }

      //-----------------------------------------------------------------------
      PromiseWithRTPMediaEnginePtr notify()
      {
        auto promise = PromiseWithRTPMediaEngine::create(IORTCForInternal::queueORTC());
        promise->setReferenceHolder(mThisWeak.lock());
        mEngine->notify(promise);
        return promise;
      }

    public:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark IRTPMediaEngineRegistration
      #pragma mark

      virtual RTPMediaEnginePtr getRTPEngine() const {return mEngine;}

    protected:
      RTPMediaEngineRegistrationWeakPtr mThisWeak;
      RTPMediaEnginePtr mEngine;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngineSingleton
    #pragma mark

    class RTPMediaEngineSingleton : public SharedRecursiveLock,
                                    public ISingletonManagerDelegate
    {
    protected:
      struct make_private {};

    public:
      //-----------------------------------------------------------------------
      RTPMediaEngineSingleton(const make_private &) :
        SharedRecursiveLock(SharedRecursiveLock::create())
      {
      }

      //-----------------------------------------------------------------------
      static RTPMediaEngineSingletonPtr create()
      {
        RTPMediaEngineSingletonPtr pThis(make_shared<RTPMediaEngineSingleton>(make_private{}));
        return pThis;
      }

      //-----------------------------------------------------------------------
      static RTPMediaEngineSingletonPtr singleton()
      {
        AutoRecursiveLock lock(*UseServicesHelper::getGlobalLock());
        static SingletonLazySharedPtr<RTPMediaEngineSingleton> singleton(create());
        RTPMediaEngineSingletonPtr result = singleton.singleton();

        static zsLib::SingletonManager::Register registerSingleton("openpeer::ortc::RTPMediaEngineSingleton", result);

        if (!result) {
          ZS_LOG_WARNING(Detail, slog("singleton gone"))
        }

        return result;
      }

      //-----------------------------------------------------------------------
      Log::Params log(const char *message) const
      {
        ElementPtr objectEl = Element::create("ortc::RTPMediaEngineSingleton");
        UseServicesHelper::debugAppend(objectEl, "id", mID);
        return Log::Params(message, objectEl);
      }

      //-----------------------------------------------------------------------
      static Log::Params slog(const char *message)
      {
        return Log::Params(message, "ortc::RTPMediaEngineSingleton");
      }

      //-----------------------------------------------------------------------
      Log::Params debug(const char *message) const
      {
        return Log::Params(message, toDebug());
      }

      //-----------------------------------------------------------------------
      virtual ElementPtr toDebug() const
      {
        AutoRecursiveLock lock(*this);
        ElementPtr resultEl = Element::create("ortc::RTPMediaEngineSingleton");

        UseServicesHelper::debugAppend(resultEl, "id", mID);

        return resultEl;
      }

      //-----------------------------------------------------------------------
      RTPMediaEngineRegistrationPtr getEngineRegistration()
      {
        AutoRecursiveLock lock(*this);
        auto result = mEngineRegistration.lock();
        if (!result) {
          result = RTPMediaEngineRegistration::create();
          mEngineRegistration = result;
        }
        return result;
      }

    protected:

      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark ISingletonManagerDelegate
      #pragma mark

      virtual void notifySingletonCleanup()
      {
        AutoRecursiveLock lock(*this);
        mEngineRegistration.reset();
      }

    protected:
      AutoPUID mID;

      RTPMediaEngineRegistrationWeakPtr mEngineRegistration;
    };

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IICETransportForSettings
    #pragma mark

    //-------------------------------------------------------------------------
    void IRTPMediaEngineForSettings::applyDefaults()
    {
//      UseSettings::setUInt(ORTC_SETTING_SCTP_TRANSPORT_MAX_MESSAGE_SIZE, 5*1024);
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPMediaEngineForRTPReceiverChannelMediaBase
    #pragma mark

    //-------------------------------------------------------------------------
    ElementPtr IRTPMediaEngineForRTPReceiverChannelMediaBase::toDebug(ForRTPReceiverChannelMediaBasePtr object)
    {
      if (!object) return ElementPtr();
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object)->toDebug();
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEnginePtr IRTPMediaEngineForRTPReceiverChannelMediaBase::create()
    {
      return RTPMediaEngine::createEnginePromise();
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineDeviceResourcePtr IRTPMediaEngineForRTPReceiverChannelMediaBase::getDeviceResource(const char *deviceID)
    {
      auto singleton = RTPMediaEngineSingleton::singleton();
      if (!singleton) return PromiseWithRTPMediaEngineDeviceResource::createRejected(IORTCForInternal::queueORTC());
      return singleton->getEngineRegistration()->getRTPEngine()->getDeviceResource(deviceID);
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineChannelResourcePtr IRTPMediaEngineForRTPReceiverChannelMediaBase::setupChannel(
                                                                                                            UseReceiverChannelMediaBasePtr channel,
                                                                                                            TransportPtr transport,
                                                                                                            MediaStreamTrackPtr track,
                                                                                                            ParametersPtr parameters
                                                                                                            )
    {
      auto singleton = RTPMediaEngineSingleton::singleton();
      if (!singleton) return PromiseWithRTPMediaEngineChannelResource::createRejected(IORTCForInternal::queueORTC());
      return singleton->getEngineRegistration()->getRTPEngine()->setupChannel(
                                                                              channel,
                                                                              transport,
                                                                              track,
                                                                              parameters
                                                                              );
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPMediaEngineForRTPSenderChannelMediaBase
    #pragma mark

    //-------------------------------------------------------------------------
    ElementPtr IRTPMediaEngineForRTPSenderChannelMediaBase::toDebug(ForRTPSenderChannelMediaBasePtr object)
    {
      if (!object) return ElementPtr();
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object)->toDebug();
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEnginePtr IRTPMediaEngineForRTPSenderChannelMediaBase::create()
    {
      return RTPMediaEngine::createEnginePromise();
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineDeviceResourcePtr IRTPMediaEngineForRTPSenderChannelMediaBase::getDeviceResource(const char *deviceID)
    {
      auto singleton = RTPMediaEngineSingleton::singleton();
      if (!singleton) return PromiseWithRTPMediaEngineDeviceResource::createRejected(IORTCForInternal::queueORTC());
      return singleton->getEngineRegistration()->getRTPEngine()->getDeviceResource(deviceID);
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineChannelResourcePtr IRTPMediaEngineForRTPSenderChannelMediaBase::setupChannel(
                                                                                                          UseSenderChannelMediaBasePtr channel,
                                                                                                          TransportPtr transport,
                                                                                                          MediaStreamTrackPtr track,
                                                                                                          ParametersPtr parameters
                                                                                                          )
    {
      auto singleton = RTPMediaEngineSingleton::singleton();
      if (!singleton) return PromiseWithRTPMediaEngineChannelResource::createRejected(IORTCForInternal::queueORTC());
      return singleton->getEngineRegistration()->getRTPEngine()->setupChannel(
                                                                              channel,
                                                                              transport,
                                                                              track,
                                                                              parameters
                                                                              );
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine
    #pragma mark
    
    //---------------------------------------------------------------------------
    const char *RTPMediaEngine::toString(States state)
    {
      switch (state) {
        case State_Pending:       return "pending";
        case State_Ready:         return "ready";
        case State_ShuttingDown:  return "shutting down";
        case State_Shutdown:      return "shutdown";
      }
      return "UNDEFINED";
    }
    
    //-------------------------------------------------------------------------
    RTPMediaEngine::RTPMediaEngine(
                                   const make_private &,
                                   IMessageQueuePtr queue,
                                   IRTPMediaEngineRegistrationPtr registration
                                   ) :
      MessageQueueAssociator(queue),
      SharedRecursiveLock(SharedRecursiveLock::create()),
      mRegistration(registration)
    {
      EventWriteOrtcRtpMediaEngineCreate(__func__, mID);
      ZS_LOG_DETAIL(debug("created"))
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::init()
    {
      AutoRecursiveLock lock(*this);
      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::~RTPMediaEngine()
    {
      if (isNoop()) return;

      ZS_LOG_DETAIL(log("destroyed"))
      mThisWeak.reset();

      cancel();
      EventWriteOrtcRtpMediaEngineDestroy(__func__, mID);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForSettingsPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPReceiverChannelPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPReceiverChannelMediaBasePtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPReceiverChannelAudioPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPReceiverChannelVideoPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPSenderChannelMediaBasePtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPSenderChannelAudioPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForRTPSenderChannelVideoPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::convert(ForMediaStreamTrackPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }

    RTPMediaEnginePtr RTPMediaEngine::convert(ForDeviceResourcePtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(RTPMediaEngine, object);
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => RTPMediaEngineSingleton/RTPMediaEngineRegistration
    #pragma mark
    
    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEnginePtr RTPMediaEngine::createEnginePromise()
    {
      auto singleton = RTPMediaEngineSingleton::singleton();
      if (!singleton) {
        return PromiseWithRTPMediaEngine::createRejected(IORTCForInternal::queueORTC());
      }
      return singleton->getEngineRegistration()->notify();
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr RTPMediaEngine::create(IRTPMediaEngineRegistrationPtr registration)
    {
      RTPMediaEnginePtr pThis(make_shared<RTPMediaEngine>(make_private {}, IORTCForInternal::queueBlockingMediaStartStopThread(), registration));
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::notify(PromiseWithRTPMediaEnginePtr promise)
    {
      IRTPMediaEngineRegistrationPtr registration;

      {
        AutoRecursiveLock lock(*this);
        if (!isReady()) {
          mPendingReady.push_back(promise);
          return;
        }
        registration = mRegistration.lock();
      }

      if (registration) {
        promise->resolve(registration->getRTPEngine());
      }
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::shutdown()
    {
      AutoRecursiveLock lock(*this);

      // WARNING: Do NOT call cancel directly as this object must only be
      // shutdown on the object's media queue.
      setState(State_ShuttingDown);
      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPReceiverChannel
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPReceiverChannelMediaBase
    #pragma mark

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineDeviceResourcePtr RTPMediaEngine::getDeviceResource(const char *deviceID)
    {
      DeviceResourcePtr resource = DeviceResource::create(mRegistration.lock(), deviceID);

      {
        AutoRecursiveLock lock(*this);
        mExampleDeviceResources[resource->getID()] = resource;
        mExamplePendingDeviceResources.push_back(resource);
      }

      // invoke "step" mechanism asynchronously to do something with this resource...
      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();

      return resource->createPromise<IRTPMediaEngineDeviceResource>();
    }

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineChannelResourcePtr RTPMediaEngine::setupChannel(
                                                                             UseReceiverChannelMediaBasePtr channel,
                                                                             TransportPtr transport,
                                                                             MediaStreamTrackPtr track,
                                                                             ParametersPtr parameters
                                                                             )
    {
       PromiseWithRTPMediaEngineChannelResourcePtr promise;

      {
        AutoRecursiveLock lock(*this);
        if (ZS_DYNAMIC_PTR_CAST(IRTPReceiverChannelAudioForRTPMediaEngine, channel)) {
          AudioReceiverChannelResourcePtr resource = AudioReceiverChannelResource::create(
                                                                                          mRegistration.lock(),
                                                                                          transport,
                                                                                          track,
                                                                                          parameters
                                                                                          );
          promise = resource->createPromise<IRTPMediaEngineChannelResource>();
          mChannelResources[channel->getID()] = resource;
          mPendingSetupChannelResources.push_back(resource);
        } else if (ZS_DYNAMIC_PTR_CAST(IRTPReceiverChannelVideoForRTPMediaEngine, channel)) {
          VideoReceiverChannelResourcePtr resource = VideoReceiverChannelResource::create(
                                                                                          mRegistration.lock(),
                                                                                          transport,
                                                                                          track,
                                                                                          parameters
                                                                                          );
          promise = resource->createPromise<IRTPMediaEngineChannelResource>();
          mChannelResources[channel->getID()] = resource;
          mPendingSetupChannelResources.push_back(resource);
        }
      }

      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();

      return promise;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPReceiverChannelAudio
    #pragma mark

    //-------------------------------------------------------------------------
    webrtc::VoiceEngine *RTPMediaEngine::getVoiceEngine()
    {
      AutoRecursiveLock lock(*this);
      return mVoiceEngine.get();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPReceiverChannelVideo
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPSenderChannel
    #pragma mark


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPSenderChannelMediaBase
    #pragma mark

    //-------------------------------------------------------------------------
    PromiseWithRTPMediaEngineChannelResourcePtr RTPMediaEngine::setupChannel(
                                                                             UseSenderChannelMediaBasePtr channel,
                                                                             TransportPtr transport,
                                                                             MediaStreamTrackPtr track,
                                                                             ParametersPtr parameters
                                                                             )
    {
      PromiseWithRTPMediaEngineChannelResourcePtr promise;

      {
        AutoRecursiveLock lock(*this);
        if (ZS_DYNAMIC_PTR_CAST(IRTPSenderChannelAudioForRTPMediaEngine, channel)) {
          ChannelResourcePtr resource = AudioSenderChannelResource::create(
                                                                           mRegistration.lock(),
                                                                           transport,
                                                                           track,
                                                                           parameters
                                                                           );
          promise = resource->createPromise<IRTPMediaEngineChannelResource>();
          mChannelResources[resource->getID()] = resource;
          mPendingSetupChannelResources.push_back(resource);
        } else if (ZS_DYNAMIC_PTR_CAST(IRTPSenderChannelVideoForRTPMediaEngine, channel)) {
          ChannelResourcePtr resource = VideoSenderChannelResource::create(
                                                                           mRegistration.lock(),
                                                                           transport,
                                                                           track,
                                                                           parameters
                                                                           );
          promise = resource->createPromise<IRTPMediaEngineChannelResource>();
          mChannelResources[resource->getID()] = resource;
          mPendingSetupChannelResources.push_back(resource);
        }
      }

      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();

      return promise;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPSenderChannelAudio
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForRTPSenderChannelVideo
    #pragma mark


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForDeviceResource
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::notifyResourceGone(IRTPMediaEngineDeviceResource &inResource)
    {
      DeviceResource &resource = dynamic_cast<DeviceResource &>(inResource);

      PUID resourceID = resource.getID();

      AutoRecursiveLock lock(*this);

      auto found = mExampleDeviceResources.find(resourceID);
      if (found != mExampleDeviceResources.end()) {
        mExampleDeviceResources.erase(found);
      }

      // invoke "step" mechanism again
      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineForChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::notifyResourceGone(IChannelResourceForRTPMediaEngine &inResource)
    {
      PUID resourceID = inResource.getID();

      AutoRecursiveLock lock(*this);

      auto found = mChannelResources.find(resourceID);
      if (found != mChannelResources.end()) {
        mChannelResources.erase(found);
      }

      // invoke "step" mechanism again
      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IWakeDelegate
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::onWake()
    {
      ZS_LOG_DEBUG(log("wake"))

      AutoRecursiveLock lock(*this);
      step();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => ITimerDelegate
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::onTimer(TimerPtr timer)
    {
      ZS_LOG_DEBUG(log("timer") + ZS_PARAM("timer id", timer->getID()))

      AutoRecursiveLock lock(*this);
      // NOTE: ADD IF NEEDED...
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => IRTPMediaEngineAsyncDelegate
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => (friend ChannelResource)
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::shutdownChannelResource(ChannelResourcePtr channelResource)
    {
      {
        AutoRecursiveLock lock(*this);
        mPendingCloseChannelResources.push_back(channelResource);
      }
      IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine => (internal)
    #pragma mark

    //-------------------------------------------------------------------------
    Log::Params RTPMediaEngine::log(const char *message) const
    {
      ElementPtr objectEl = Element::create("ortc::RTPMediaEngine");
      UseServicesHelper::debugAppend(objectEl, "id", mID);
      return Log::Params(message, objectEl);
    }

    //-------------------------------------------------------------------------
    Log::Params RTPMediaEngine::debug(const char *message) const
    {
      return Log::Params(message, toDebug());
    }

    //-------------------------------------------------------------------------
    ElementPtr RTPMediaEngine::toDebug() const
    {
      AutoRecursiveLock lock(*this);

      ElementPtr resultEl = Element::create("ortc::RTPMediaEngine");

      UseServicesHelper::debugAppend(resultEl, "id", mID);

      UseServicesHelper::debugAppend(resultEl, "graceful shutdown", (bool)mGracefulShutdownReference);

      UseServicesHelper::debugAppend(resultEl, "state", toString(mCurrentState));

      UseServicesHelper::debugAppend(resultEl, "error", mLastError);
      UseServicesHelper::debugAppend(resultEl, "error reason", mLastErrorReason);

      auto registration = mRegistration.lock();
      UseServicesHelper::debugAppend(resultEl, "registration", (bool)registration);

      UseServicesHelper::debugAppend(resultEl, "pending ready", mPendingReady.size());

      UseServicesHelper::debugAppend(resultEl, "device resources", mExampleDeviceResources.size());
      UseServicesHelper::debugAppend(resultEl, "pending device resources", mExamplePendingDeviceResources.size());

      UseServicesHelper::debugAppend(resultEl, "channel resources", mChannelResources.size());
      UseServicesHelper::debugAppend(resultEl, "pending setup channel resources", mPendingSetupChannelResources.size());
      UseServicesHelper::debugAppend(resultEl, "pending close channel resources", mPendingCloseChannelResources.size());

      return resultEl;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::isReady() const
    {
      return State_Ready == mCurrentState;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::isShuttingDown() const
    {
      return State_ShuttingDown == mCurrentState;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::isShutdown() const
    {
      return State_Shutdown == mCurrentState;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::step()
    {
      ZS_LOG_DEBUG(debug("step"))

      if ((isShuttingDown()) ||
          (isShutdown())) {
        ZS_LOG_DEBUG(debug("step forwarding to cancel"))
        stepCancel();
        return;
      }

      // ... other steps here ...
      if (!stepSetup()) goto not_ready;
      if (!stepExampleSetupDeviceResources()) goto not_ready;
      if (!stepSetupChannels()) goto not_ready;
      if (!stepCloseChannels()) goto not_ready;
      // ... other steps here ...

      goto ready;

    not_ready:
      {
        ZS_LOG_TRACE(debug("not ready"))
        return;
      }

    ready:
      {
        ZS_LOG_TRACE(log("ready"))
        setState(State_Ready);
      }
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::stepSetup()
    {
      if (isReady()) {
        ZS_LOG_TRACE(log("already setup"))
        return true;
      }

      mVoiceEngine = rtc::scoped_ptr<webrtc::VoiceEngine, VoiceEngineDeleter>(webrtc::VoiceEngine::Create());

      return true;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::stepExampleSetupDeviceResources()
    {
      while (mExamplePendingDeviceResources.size() > 0) {
        auto deviceResource = mExamplePendingDeviceResources.front().lock();

        if (deviceResource) {
          // Only remember WEAK pointer to device so it's possible the example
          // device resource was already destroyed. Thus only setup the device
          // if the object is still alive.
          deviceResource->notifyPromisesResolve();
        }

        mExamplePendingDeviceResources.pop_front();
      }

      return true;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::stepSetupChannels()
    {
      while (mPendingSetupChannelResources.size() > 0) {
        auto channelResource = mPendingSetupChannelResources.front();

        channelResource->notifySetup();

        mPendingSetupChannelResources.pop_front();
      }

      return true;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::stepCloseChannels()
    {
      while (mPendingCloseChannelResources.size() > 0) {
        auto channelResource = mPendingCloseChannelResources.front();

        channelResource->notifyShutdown();
        
        mPendingCloseChannelResources.pop_front();
      }

      return true;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::cancel()
    {
      //.......................................................................
      // try to gracefully shutdown

      if (isShutdown()) return;

      setState(State_ShuttingDown);

      if (!mGracefulShutdownReference) mGracefulShutdownReference = mThisWeak.lock();

      {
        for (auto iter = mPendingSetupChannelResources.begin(); iter != mPendingSetupChannelResources.end(); ++iter)
        {
          auto channelResource = (*iter);
          channelResource->notifyPromisesReject();
        }
        mPendingSetupChannelResources.clear();
      }

      if (mGracefulShutdownReference) {
        // perform any graceful asynchronous shutdown processes needed and
        // re-attempt shutdown again later if needed.

#define TODO_IMPLEMENT_MEDIA_GRACEFUL_SHUTDOWN 1
#define TODO_IMPLEMENT_MEDIA_GRACEFUL_SHUTDOWN 2
//        return;
      }

      //.......................................................................
      // final cleanup (hard shutdown)

      setState(State_Shutdown);

#define TODO_IMPLEMENT_MEDIA_HARD_SHUTDOWN 1
#define TODO_IMPLEMENT_MEDIA_HARD_SHUTDOWN 2

      // resolve any outstanding promises
      {
        auto registration = mRegistration.lock();
        while (mPendingReady.size() > 0)
        {
          auto &front = mPendingReady.front();
          if (registration) {
            front->resolve(registration);
          } else {
            front->reject();
          }
          mPendingReady.pop_front();
        }
      }

      // make sure to cleanup any final reference to self
      mGracefulShutdownReference.reset();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::stepCancel()
    {
      {
        for (auto iter = mPendingCloseChannelResources.begin(); iter != mPendingCloseChannelResources.end(); ++iter)
        {
          auto channelResource = (*iter);
          channelResource->notifyShutdown();
        }
        mPendingCloseChannelResources.clear();
      }

      {
        for (auto iter = mChannelResources.begin(); iter != mChannelResources.end(); ++iter)
        {
          auto channelResource = (*iter).second.lock();
          channelResource->notifyShutdown();
        }

        mChannelResources.clear();
      }
      cancel();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::setState(States state)
    {
      if (state == mCurrentState) return;

      ZS_LOG_DETAIL(debug("state changed") + ZS_PARAM("new state", toString(state)) + ZS_PARAM("old state", toString(mCurrentState)))

      mCurrentState = state;

      if (isReady()) {
        auto registration = mRegistration.lock();

        while (mPendingReady.size() > 0)
        {
          auto &front = mPendingReady.front();
          if (registration) {
            front->resolve(registration->getRTPEngine());
          } else {
            front->reject();
          }
          mPendingReady.pop_front();
        }
      }

//      RTPMediaEnginePtr pThis = mThisWeak.lock();
//      if (pThis) {
//        mSubscriptions.delegate()->onRTPMediaEngineStateChanged(pThis, mCurrentState);
//      }
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::setError(WORD errorCode, const char *inReason)
    {
      String reason(inReason);
      if (reason.isEmpty()) {
        reason = UseHTTP::toString(UseHTTP::toStatusCode(errorCode));
      }

      if (0 != mLastError) {
        ZS_LOG_WARNING(Detail, debug("error already set thus ignoring new error") + ZS_PARAM("new error", errorCode) + ZS_PARAM("new reason", reason))
        return;
      }

      mLastError = errorCode;
      mLastErrorReason = reason;

      ZS_LOG_WARNING(Detail, debug("error set") + ZS_PARAM("error", mLastError) + ZS_PARAM("reason", mLastErrorReason))
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::BaseResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::BaseResource::BaseResource(
                                               const make_private &,
                                               IMessageQueuePtr queue,
                                               IRTPMediaEngineRegistrationPtr registration
                                               ) :
      SharedRecursiveLock(SharedRecursiveLock::create()),
      MessageQueueAssociator(queue),
      mRegistration(registration),
      mMediaEngine(registration ? registration->getRTPEngine() : RTPMediaEnginePtr())
    {
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::BaseResource::~BaseResource()
    {
      mThisWeak.reset();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::BaseResource::notifyPromisesResolve()
    {
      {
        AutoRecursiveLock lock(*this);
        mNotifiedReady = true;
      }
      internalFixState();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::BaseResource::notifyPromisesReject()
    {
      {
        AutoRecursiveLock lock(*this);
        mNotifiedRejected = true;
      }
      internalFixState();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::BaseResource => (internal)
    #pragma mark


    //-------------------------------------------------------------------------
    IMessageQueuePtr RTPMediaEngine::BaseResource::delegateQueue()
    {
      return IORTCForInternal::queueORTC();
    }

    //-------------------------------------------------------------------------
    PromisePtr RTPMediaEngine::BaseResource::internalSetupPromise(PromisePtr promise)
    {
      promise->setReferenceHolder(mThisWeak.lock());

      {
        AutoRecursiveLock lock(*this);
        mPendingPromises.push_back(promise);
      }
      internalFixState();
      return promise;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::BaseResource::internalFixState()
    {
      PendingPromiseList promises;

      {
        AutoRecursiveLock lock(*this);

        {
          if (mNotifiedRejected) goto prepare_reject_all;
          if (mNotifiedReady) goto prepare_resolve_all;
          return;
        }

      prepare_resolve_all:
        {
          promises = mPendingPromises;
          mPendingPromises.clear();
          goto resolve_all;
        }

      prepare_reject_all:
        {
          promises = mPendingPromises;
          mPendingPromises.clear();
          goto reject_all;
        }
      }

    resolve_all:
      {
        for (auto iter = promises.begin(); iter != promises.end(); ++iter) {
          auto promise = (*iter).lock();
          if (!promise) continue;
          promise->resolve(mThisWeak.lock());
        }
        return;
      }

    reject_all:
      {
        for (auto iter = promises.begin(); iter != promises.end(); ++iter) {
          auto promise = (*iter).lock();
          if (!promise) continue;
          promise->reject();
        }
      }
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::DeviceResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::DeviceResource::DeviceResource(
                                                   const make_private &priv,
                                                   IMessageQueuePtr queue,
                                                   IRTPMediaEngineRegistrationPtr registration,
                                                   const char *deviceID
                                                   ) :
      BaseResource(priv, queue, registration),
      mDeviceID(deviceID)
    {
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::DeviceResource::~DeviceResource()
    {
      mThisWeak.reset();  // shared pointer to self is no longer valid

      // inform the rtp media engine of this resource no longer being in use
      UseEnginePtr engine = getEngine<UseEngine>();
      if (engine) {
        engine->notifyResourceGone(*this);
      }
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::DeviceResourcePtr RTPMediaEngine::DeviceResource::create(
                                                                             IRTPMediaEngineRegistrationPtr registration,
                                                                             const char *deviceID
                                                                             )
    {
      auto pThis = make_shared<DeviceResource>(make_private{}, IORTCForInternal::queueBlockingMediaStartStopThread(), registration, deviceID);
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::DeviceResource::init()
    {
      DeviceResourcePtr pThis = getThis<DeviceResource>();  // example of how to get pThis from base class
#define EXAMPLE_OF_HOW_TO_DO_CUSTOM_RESOURC_SETUP_STEP 1
#define EXAMPLE_OF_HOW_TO_DO_CUSTOM_RESOURC_SETUP_STEP 2
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::DeviceResource => IRTPMediaEngineDeviceResource
    #pragma mark

    //-------------------------------------------------------------------------
    String RTPMediaEngine::DeviceResource::getDeviceID() const
    {
#define EXAMPLE_OF_HOW_TO_EXPOSE_API_TO_CONSUMER_OF_RESOURCE 1
#define EXAMPLE_OF_HOW_TO_EXPOSE_API_TO_CONSUMER_OF_RESOURCE 2
      return mDeviceID;
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::ChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::ChannelResource::~ChannelResource()
    {
      mThisWeak.reset();
      UseEnginePtr engine = getEngine<UseEngine>();
      if (engine) {
        engine->notifyResourceGone(*this);
      }
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::ChannelResource => IRTPMediaEngineChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    PromisePtr RTPMediaEngine::ChannelResource::shutdown()
    {
      PromisePtr promise = getShutdownPromise();
      if (isShutdown()) return promise;
      if (mShuttingDown) return promise;

      mShuttingDown = true;

      auto outer = mMediaEngine.lock();
      if (outer) {
        outer->shutdownChannelResource(ZS_DYNAMIC_PTR_CAST(ChannelResource, mThisWeak.lock()));
      } else {
        notifyShutdown();
      }
      return promise;
    }
    

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::ChannelResource => (internal friend derived)
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::ChannelResource::notifyPromisesShutdown()
    {
      AutoRecursiveLock lock(*this);

      mShutdown = true;
      mShuttingDown = false;

      for (auto iter = mShutdownPromises.begin(); iter != mShutdownPromises.end(); ++iter)
      {
        auto promise = (*iter);
        promise->resolve();
      }
      mShutdownPromises.clear();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::ChannelResource => (internal)
    #pragma mark

    //-------------------------------------------------------------------------
    PromisePtr RTPMediaEngine::ChannelResource::getShutdownPromise()
    {
      if (isShutdown()) {
        return Promise::createResolved(delegateQueue());
      }
      PromisePtr promise = Promise::create(delegateQueue());
      mShutdownPromises.push_back(promise);
      return promise;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioReceiverChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::AudioReceiverChannelResource::AudioReceiverChannelResource(
                                                                               const make_private &priv,
                                                                               IMessageQueuePtr queue,
                                                                               IRTPMediaEngineRegistrationPtr registration,
                                                                               TransportPtr transport,
                                                                               MediaStreamTrackPtr track,
                                                                               ParametersPtr parameters
                                                                               ) :
      ChannelResource(priv, queue, registration),
      mTransport(transport),
      mTrack(track),
      mParameters(parameters)
    {
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::AudioReceiverChannelResource::~AudioReceiverChannelResource()
    {
      mThisWeak.reset();  // shared pointer to self is no longer valid
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::AudioReceiverChannelResourcePtr RTPMediaEngine::AudioReceiverChannelResource::create(
                                                                                                         IRTPMediaEngineRegistrationPtr registration,
                                                                                                         TransportPtr transport,
                                                                                                         MediaStreamTrackPtr track,
                                                                                                         ParametersPtr parameters
                                                                                                         )
    {
      auto pThis = make_shared<AudioReceiverChannelResource>(
                                                             make_private{},
                                                             IORTCForInternal::queueBlockingMediaStartStopThread(),
                                                             registration,
                                                             transport,
                                                             track,
                                                             parameters
                                                             );
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::AudioReceiverChannelResource::init()
    {
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioReceiverChannelResource => IRTPMediaEngineChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioReceiverChannelResource => IRTPMediaEngineAudioReceiverChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::AudioReceiverChannelResource::handlePacket(const RTPPacket &packet)
    {
      webrtc::PacketTime time(packet.timestamp(), 0);

      auto engine = mMediaEngine.lock();
      if (!engine) return false;

      auto voiceEngine = engine->getVoiceEngine();
      if (!voiceEngine) return false;

      webrtc::VoENetwork::GetInterface(voiceEngine)->ReceivedRTPPacket(getChannel(), packet.ptr(), packet.size(), time);
      return true;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::AudioReceiverChannelResource::handlePacket(const RTCPPacket &packet)
    {
      auto engine = mMediaEngine.lock();
      if (!engine) return false;

      auto voiceEngine = engine->getVoiceEngine();
      if (!voiceEngine) return false;

      webrtc::VoENetwork::GetInterface(voiceEngine)->ReceivedRTCPPacket(getChannel(), packet.ptr(), packet.size());
      return true;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioReceiverChannelResource => friend RTPMediaEngine
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::AudioReceiverChannelResource::notifySetup()
    {
      AutoRecursiveLock lock(*this);

      auto engine = mMediaEngine.lock();
      if (!engine) {
        notifyPromisesReject();
        return;
      }

      auto voiceEngine = engine->getVoiceEngine();
      if (NULL == voiceEngine) {
        notifyPromisesReject();
        return;
      }

      mModuleProcessThread = webrtc::ProcessThread::Create("AudioReceiverChannelResourceThread");

      mCallStats = rtc::scoped_ptr<webrtc::CallStats>(new webrtc::CallStats());
      mCongestionController =
        rtc::scoped_ptr<webrtc::CongestionController>(new webrtc::CongestionController(
                                                                                       mModuleProcessThread.get(),
                                                                                       mCallStats.get())
                                                                                       );

      mModuleProcessThread->Start();
      mModuleProcessThread->RegisterModule(mCallStats.get());

      webrtc::VoEBase::GetInterface(voiceEngine)->Init(mTrack->getAudioDeviceModule());

      mChannel = webrtc::VoEBase::GetInterface(voiceEngine)->CreateChannel();

      webrtc::CodecInst codec;
      IRTPTypes::CodecParametersList::iterator codecIter = mParameters->mCodecs.begin();
      while (codecIter != mParameters->mCodecs.end()) {
        auto supportedCodec = IRTPTypes::toSupportedCodec(codecIter->mName);
        if (IRTPTypes::SupportedCodec_Opus == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetRecPayloadType(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_Isac == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetRecPayloadType(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_G722 == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetRecPayloadType(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_ILBC == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetRecPayloadType(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_PCMU == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetRecPayloadType(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_PCMA == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetRecPayloadType(mChannel, codec);
          break;
        }
        IRTPTypes::RTCPFeedbackList::iterator rtcpFeedbackIter = codecIter->mRTCPFeedback.begin();
        while (rtcpFeedbackIter != codecIter->mRTCPFeedback.end()) {
          IRTPTypes::KnownFeedbackTypes feedbackType = IRTPTypes::toKnownFeedbackType(rtcpFeedbackIter->mType);
          IRTPTypes::KnownFeedbackParameters feedbackParameter = IRTPTypes::toKnownFeedbackParameter(rtcpFeedbackIter->mParameter);
          if (IRTPTypes::KnownFeedbackType_NACK == feedbackType && IRTPTypes::KnownFeedbackParameter_Unknown == feedbackParameter) {
            webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetNACKStatus(mChannel, true, 250);
          }
          rtcpFeedbackIter++;
        }
        codecIter++;
      }

      webrtc::AudioReceiveStream::Config config;
      config.voe_channel_id = mChannel;

      IRTPTypes::EncodingParametersList::iterator encodingParamIter = mParameters->mEncodings.begin();
      while (encodingParamIter != mParameters->mEncodings.end()) {
        if (encodingParamIter->mCodecPayloadType == codec.pltype) {
          config.rtp.remote_ssrc = encodingParamIter->mSSRC;
          break;
        }
        encodingParamIter++;
      }

      IRTPTypes::HeaderExtensionParametersList::iterator headerExtensionIter = mParameters->mHeaderExtensions.begin();
      while (headerExtensionIter != mParameters->mHeaderExtensions.end()) {
        IRTPTypes::HeaderExtensionURIs headerExtensionURI = IRTPTypes::toHeaderExtensionURI(headerExtensionIter->mURI);
        switch (headerExtensionURI) {
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_ClienttoMixerAudioLevelIndication:
          webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetReceiveAudioLevelIndicationStatus(mChannel, true, headerExtensionIter->mID);
          config.rtp.extensions.push_back(webrtc::RtpExtension(headerExtensionIter->mURI, headerExtensionIter->mID));
          break;
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_AbsoluteSendTime:
          webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetReceiveAbsoluteSenderTimeStatus(mChannel, true, headerExtensionIter->mID);
          config.rtp.extensions.push_back(webrtc::RtpExtension(headerExtensionIter->mURI, headerExtensionIter->mID));
          break;
        default:
          break;
        }
        headerExtensionIter++;
      }

      webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetLocalSSRC(mChannel, mParameters->mRTCP.mSSRC);
      config.rtp.local_ssrc = mParameters->mRTCP.mSSRC;
      config.receive_transport = mTransport.get();
      config.rtcp_send_transport = mTransport.get();
      config.combined_audio_video_bwe = true;

      mReceiveStream = rtc::scoped_ptr<webrtc::AudioReceiveStream>(
        new webrtc::internal::AudioReceiveStream(
                                                 mCongestionController->GetRemoteBitrateEstimator(false),
                                                 config,
                                                 voiceEngine
                                                 ));

      webrtc::VoENetwork::GetInterface(voiceEngine)->RegisterExternalTransport(mChannel, *mTransport);

      mTrack->start();

      webrtc::VoEBase::GetInterface(voiceEngine)->StartReceive(mChannel);
      webrtc::VoEBase::GetInterface(voiceEngine)->StartPlayout(mChannel);

      notifyPromisesResolve();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::AudioReceiverChannelResource::notifyShutdown()
    {
      AutoRecursiveLock lock(*this);

      auto outer = mMediaEngine.lock();

      if (outer) {
        auto voiceEngine = outer->getVoiceEngine();
        if (voiceEngine) {
          webrtc::VoEBase::GetInterface(voiceEngine)->StopPlayout(mChannel);
          webrtc::VoEBase::GetInterface(voiceEngine)->StopReceive(mChannel);
          webrtc::VoENetwork::GetInterface(voiceEngine)->DeRegisterExternalTransport(mChannel);
        }
      }

#define FIX_ME_WARNING_NO_TRACK_IS_NOT_STOPPED_JUST_BECAUSE_A_RECEIVER_CHANNEL_IS_DONE 1
#define FIX_ME_WARNING_NO_TRACK_IS_NOT_STOPPED_JUST_BECAUSE_A_RECEIVER_CHANNEL_IS_DONE 2

      if (mTrack)
        mTrack->stop();

      mModuleProcessThread->DeRegisterModule(mCallStats.get());
      mModuleProcessThread->Stop();

      mReceiveStream.reset();
      mCongestionController.reset();
      mCallStats.reset();
      mModuleProcessThread.reset();

      notifyPromisesShutdown();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioReceiverChannelResource => (internal)
    #pragma mark

    //-------------------------------------------------------------------------
    int RTPMediaEngine::AudioReceiverChannelResource::getChannel() const
    {
      AutoRecursiveLock lock(*this);
      return mChannel;
    }

    //-------------------------------------------------------------------------
    webrtc::CodecInst RTPMediaEngine::AudioReceiverChannelResource::getAudioCodec(
                                                                                  webrtc::VoiceEngine *voiceEngine,
                                                                                  String payloadName
                                                                                  )
    {
      webrtc::CodecInst codec;
      int numOfCodecs = webrtc::VoECodec::GetInterface(voiceEngine)->NumOfCodecs();
      for (int i = 0; i < numOfCodecs; ++i) {
        webrtc::CodecInst currentCodec;
        webrtc::VoECodec::GetInterface(voiceEngine)->GetCodec(i, currentCodec);
        if (0 == String(currentCodec.plname).compareNoCase(payloadName)) {
          codec = currentCodec;
          break;
        }
      }

      return codec;
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioSenderChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::AudioSenderChannelResource::AudioSenderChannelResource(
                                                                           const make_private &priv,
                                                                           IMessageQueuePtr queue,
                                                                           IRTPMediaEngineRegistrationPtr registration,
                                                                           TransportPtr transport,
                                                                           MediaStreamTrackPtr track,
                                                                           ParametersPtr parameters
                                                                           ) :
      ChannelResource(priv, queue, registration),
      mTransport(transport),
      mTrack(track),
      mParameters(parameters)
    {
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::AudioSenderChannelResource::~AudioSenderChannelResource()
    {
      mThisWeak.reset();  // shared pointer to self is no longer valid
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::AudioSenderChannelResourcePtr RTPMediaEngine::AudioSenderChannelResource::create(
                                                                                                     IRTPMediaEngineRegistrationPtr registration,
                                                                                                     TransportPtr transport,
                                                                                                     MediaStreamTrackPtr track,
                                                                                                     ParametersPtr parameters
                                                                                                     )
    {
      auto pThis = make_shared<AudioSenderChannelResource>(
                                                           make_private{},
                                                           IORTCForInternal::queueBlockingMediaStartStopThread(),
                                                           registration,
                                                           transport,
                                                           track,
                                                           parameters
                                                           );
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::AudioSenderChannelResource::init()
    {
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioSenderChannelResource => IRTPMediaEngineChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioSenderChannelResource => IRTPMediaEngineAudioSenderChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::AudioSenderChannelResource::handlePacket(const RTCPPacket &packet)
    {

      webrtc::AudioSendStream *stream = NULL;

      {
        AutoRecursiveLock lock(*this);

        stream = mSendStream.get();
        if (NULL == stream) return false;

        ++mAccessFromNonLockedMethods;
      }

      bool result = stream->DeliverRtcp(packet.ptr(), packet.size());
      --mAccessFromNonLockedMethods;
      return result;
    }
    
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioSenderChannelResource => friend RTPMediaEngine
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::AudioSenderChannelResource::notifySetup()
    {
      AutoRecursiveLock lock(*this);

      auto engine = mMediaEngine.lock();
      if (!engine) {
        notifyPromisesReject();
        return;
      }

      auto voiceEngine = engine->getVoiceEngine();
      if (!voiceEngine) {
        notifyPromisesReject();
        return;
      }

      webrtc::VoEBase::GetInterface(voiceEngine)->Init(mTrack->getAudioDeviceModule());

      mChannel = webrtc::VoEBase::GetInterface(voiceEngine)->CreateChannel();

      webrtc::CodecInst codec;
      IRTPTypes::CodecParametersList::iterator codecIter = mParameters->mCodecs.begin();
      while (codecIter != mParameters->mCodecs.end()) {
        auto supportedCodec = IRTPTypes::toSupportedCodec(codecIter->mName);
        if (IRTPTypes::SupportedCodec_Opus == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetSendCodec(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_Isac == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetSendCodec(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_G722 == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetSendCodec(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_ILBC == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetSendCodec(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_PCMU == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetSendCodec(mChannel, codec);
          break;
        } else if (IRTPTypes::SupportedCodec_PCMA == supportedCodec) {
          codec = getAudioCodec(voiceEngine, codecIter->mName);
          webrtc::VoECodec::GetInterface(voiceEngine)->SetSendCodec(mChannel, codec);
          break;
        }
        IRTPTypes::RTCPFeedbackList::iterator rtcpFeedbackIter = codecIter->mRTCPFeedback.begin();
        while (rtcpFeedbackIter != codecIter->mRTCPFeedback.end()) {
          IRTPTypes::KnownFeedbackTypes feedbackType = IRTPTypes::toKnownFeedbackType(rtcpFeedbackIter->mType);
          IRTPTypes::KnownFeedbackParameters feedbackParameter = IRTPTypes::toKnownFeedbackParameter(rtcpFeedbackIter->mParameter);
          if (IRTPTypes::KnownFeedbackType_NACK == feedbackType && IRTPTypes::KnownFeedbackParameter_Unknown == feedbackParameter) {
            webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetNACKStatus(mChannel, true, 250);
          }
          rtcpFeedbackIter++;
        }
        codecIter++;
      }

      webrtc::AudioSendStream::Config config(mTransport.get());
      config.voe_channel_id = mChannel;

      IRTPTypes::EncodingParametersList::iterator encodingParamIter = mParameters->mEncodings.begin();
      while (encodingParamIter != mParameters->mEncodings.end()) {
        if (encodingParamIter->mCodecPayloadType == codec.pltype) {
          webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetLocalSSRC(mChannel, encodingParamIter->mSSRC);
          config.rtp.ssrc = encodingParamIter->mSSRC;
          break;
        }
        encodingParamIter++;
      }

      IRTPTypes::HeaderExtensionParametersList::iterator headerExtensionIter = mParameters->mHeaderExtensions.begin();
      while (headerExtensionIter != mParameters->mHeaderExtensions.end()) {
        IRTPTypes::HeaderExtensionURIs headerExtensionURI = IRTPTypes::toHeaderExtensionURI(headerExtensionIter->mURI);
        switch (headerExtensionURI) {
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_ClienttoMixerAudioLevelIndication:
          webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetSendAudioLevelIndicationStatus(mChannel, true, headerExtensionIter->mID);
          config.rtp.extensions.push_back(webrtc::RtpExtension(headerExtensionIter->mURI, headerExtensionIter->mID));
          break;
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_AbsoluteSendTime:
          webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetSendAbsoluteSenderTimeStatus(mChannel, true, headerExtensionIter->mID);
          config.rtp.extensions.push_back(webrtc::RtpExtension(headerExtensionIter->mURI, headerExtensionIter->mID));
          break;
        default:
          break;
        }
        headerExtensionIter++;
      }

      webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetRTCPStatus(mChannel, true);
      webrtc::VoERTP_RTCP::GetInterface(voiceEngine)->SetRTCP_CNAME(mChannel, mParameters->mRTCP.mCName);

      mSendStream = rtc::scoped_ptr<webrtc::AudioSendStream>(
        new webrtc::internal::AudioSendStream(
                                              config,
                                              voiceEngine
                                              ));

      webrtc::VoENetwork::GetInterface(voiceEngine)->RegisterExternalTransport(mChannel, *mTransport);

      mTrack->start();

      webrtc::VoEBase::GetInterface(voiceEngine)->StartSend(mChannel);

      notifyPromisesResolve();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::AudioSenderChannelResource::notifyShutdown()
    {
      // rare race condition that can happen so
      while (mAccessFromNonLockedMethods > 0)
      {
        // NOTE: very temporary lock so should clear itself out fast
        std::this_thread::yield();
      }

      AutoRecursiveLock lock(*this);

      auto engine = mMediaEngine.lock();
      if (engine) {
        auto voiceEngine = engine->getVoiceEngine();
        if (voiceEngine) {
          webrtc::VoENetwork::GetInterface(voiceEngine)->DeRegisterExternalTransport(mChannel);
          webrtc::VoEBase::GetInterface(voiceEngine)->StopSend(mChannel);
        }
      }

      if (mTrack)
        mTrack->stop();

      mSendStream.reset();

      notifyPromisesShutdown();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::AudioSenderChannelResource => (internal)
    #pragma mark

    //-------------------------------------------------------------------------
    webrtc::CodecInst RTPMediaEngine::AudioSenderChannelResource::getAudioCodec(
                                                                                webrtc::VoiceEngine *voiceEngine,
                                                                                String payloadName
                                                                                )
    {
      webrtc::CodecInst codec;
      int numOfCodecs = webrtc::VoECodec::GetInterface(voiceEngine)->NumOfCodecs();
      for (int i = 0; i < numOfCodecs; ++i) {
        webrtc::CodecInst currentCodec;
        webrtc::VoECodec::GetInterface(voiceEngine)->GetCodec(i, currentCodec);
        if (0 == String(currentCodec.plname).compareNoCase(payloadName)) {
          codec = currentCodec;
          break;
        }
      }
      return codec;
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoReceiverChannelResource::ReceiverVideoRenderer
    #pragma mark

    //---------------------------------------------------------------------------
    void RTPMediaEngine::VideoReceiverChannelResource::ReceiverVideoRenderer::setMediaStreamTrack(UseMediaStreamTrackPtr videoTrack)
    {
      mVideoTrack = videoTrack;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoReceiverChannelResource::ReceiverVideoRenderer::RenderFrame(const webrtc::VideoFrame& video_frame, int time_to_render_ms)
    {
      mVideoTrack->renderVideoFrame(video_frame);
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::VideoReceiverChannelResource::ReceiverVideoRenderer::IsTextureSupported() const
    {
      return false;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoReceiverChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::VideoReceiverChannelResource::VideoReceiverChannelResource(
                                                                               const make_private &priv,
                                                                               IMessageQueuePtr queue,
                                                                               IRTPMediaEngineRegistrationPtr registration,
                                                                               TransportPtr transport,
                                                                               MediaStreamTrackPtr track,
                                                                               ParametersPtr parameters
                                                                               ) :
      ChannelResource(priv, queue, registration),
      mTransport(transport),
      mTrack(track),
      mParameters(parameters)
    {
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::VideoReceiverChannelResource::~VideoReceiverChannelResource()
    {
      mThisWeak.reset();  // shared pointer to self is no longer valid
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::VideoReceiverChannelResourcePtr RTPMediaEngine::VideoReceiverChannelResource::create(
                                                                                                         IRTPMediaEngineRegistrationPtr registration,
                                                                                                         TransportPtr transport,
                                                                                                         MediaStreamTrackPtr track,
                                                                                                         ParametersPtr parameters
                                                                                                         )
    {
      auto pThis = make_shared<VideoReceiverChannelResource>(
                                                             make_private{},
                                                             IORTCForInternal::queueBlockingMediaStartStopThread(),
                                                             registration,
                                                             transport,
                                                             track,
                                                             parameters
                                                             );
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoReceiverChannelResource::init()
    {
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoReceiverChannelResource => IRTPMediaEngineChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoReceiverChannelResource => IRTPMediaEngineVideoReceiverChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::VideoReceiverChannelResource::handlePacket(const RTPPacket &packet)
    {
      webrtc::VideoReceiveStream *stream = NULL;

      {
        AutoRecursiveLock lock(*this);

        stream = mReceiveStream.get();
        if (NULL == stream) return false;

        ++mAccessFromNonLockedMethods;
      }

      webrtc::PacketTime time(packet.timestamp(), 0);
      bool result = stream->DeliverRtp(packet.ptr(), packet.size(), time);
      --mAccessFromNonLockedMethods;
      return result;
    }

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::VideoReceiverChannelResource::handlePacket(const RTCPPacket &packet)
    {
      webrtc::VideoReceiveStream *stream = NULL;

      {
        AutoRecursiveLock lock(*this);

        stream = mReceiveStream.get();
        if (NULL == stream) return false;

        ++mAccessFromNonLockedMethods;
      }

      bool result = stream->DeliverRtcp(packet.ptr(), packet.size());
      --mAccessFromNonLockedMethods;
      return result;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoReceiverChannelResource => friend RTPMediaEngine
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoReceiverChannelResource::notifySetup()
    {
      AutoRecursiveLock lock(*this);

      mModuleProcessThread = webrtc::ProcessThread::Create("VideoReceiverChannelResourceThread");

      mReceiverVideoRenderer.setMediaStreamTrack(mTrack);

      mCallStats = rtc::scoped_ptr<webrtc::CallStats>(new webrtc::CallStats());
      mCongestionController =
        rtc::scoped_ptr<webrtc::CongestionController>(new webrtc::CongestionController(
                                                                                       mModuleProcessThread.get(),
                                                                                       mCallStats.get())
                                                                                       );

      mModuleProcessThread->Start();
      mModuleProcessThread->RegisterModule(mCallStats.get());

      int numCpuCores = webrtc::CpuInfo::DetectNumberOfCores();

      webrtc::Transport* transport = mTransport.get();
      webrtc::VideoReceiveStream::Config config(transport);
      webrtc::VideoReceiveStream::Decoder decoder;

      IRTPTypes::CodecParametersList::iterator codecIter = mParameters->mCodecs.begin();
      while (codecIter != mParameters->mCodecs.end()) {
        auto supportedCodec = IRTPTypes::toSupportedCodec(codecIter->mName);
        if (IRTPTypes::SupportedCodec_VP8 == supportedCodec) {
          webrtc::VideoDecoder* videoDecoder = webrtc::VideoDecoder::Create(webrtc::VideoDecoder::kVp8);
          decoder.decoder = videoDecoder;
          decoder.payload_name = codecIter->mName;
          decoder.payload_type = codecIter->mPayloadType;
          break;
        } else if (IRTPTypes::SupportedCodec_VP9 == supportedCodec) {
          webrtc::VideoDecoder* videoDecoder = webrtc::VideoDecoder::Create(webrtc::VideoDecoder::kVp9);
          decoder.decoder = videoDecoder;
          decoder.payload_name = codecIter->mName;
          decoder.payload_type = codecIter->mPayloadType;
          break;
        } else if (IRTPTypes::SupportedCodec_H264 == supportedCodec) {
          webrtc::VideoDecoder* videoDecoder = webrtc::VideoDecoder::Create(webrtc::VideoDecoder::kH264);
          decoder.decoder = videoDecoder;
          decoder.payload_name = codecIter->mName;
          decoder.payload_type = codecIter->mPayloadType;
          break;
        }
        IRTPTypes::RTCPFeedbackList::iterator rtcpFeedbackIter = codecIter->mRTCPFeedback.begin();
        while (rtcpFeedbackIter != codecIter->mRTCPFeedback.end()) {
          IRTPTypes::KnownFeedbackTypes feedbackType = IRTPTypes::toKnownFeedbackType(rtcpFeedbackIter->mType);
          IRTPTypes::KnownFeedbackParameters feedbackParameter = IRTPTypes::toKnownFeedbackParameter(rtcpFeedbackIter->mParameter);
          if (IRTPTypes::KnownFeedbackType_NACK == feedbackType && IRTPTypes::KnownFeedbackParameter_Unknown == feedbackParameter) {
            config.rtp.nack.rtp_history_ms = 1000;
          }
          else if (IRTPTypes::KnownFeedbackType_REMB == feedbackType && IRTPTypes::KnownFeedbackParameter_Unknown == feedbackParameter) {
            config.rtp.remb = true;
          }
          rtcpFeedbackIter++;
        }
        codecIter++;
      }

      IRTPTypes::EncodingParametersList::iterator encodingParamIter = mParameters->mEncodings.begin();
      while (encodingParamIter != mParameters->mEncodings.end()) {
        if (encodingParamIter->mCodecPayloadType == decoder.payload_type) {
          config.rtp.remote_ssrc = encodingParamIter->mSSRC;
          break;
        }
      }
      if (config.rtp.remote_ssrc == 0)
        config.rtp.remote_ssrc = 1000;
      config.rtp.local_ssrc = mParameters->mRTCP.mSSRC;
      if (config.rtp.local_ssrc == 0)
        config.rtp.local_ssrc = 1010;

      IRTPTypes::HeaderExtensionParametersList::iterator headerExtensionIter = mParameters->mHeaderExtensions.begin();
      while (headerExtensionIter != mParameters->mHeaderExtensions.end()) {
        IRTPTypes::HeaderExtensionURIs headerExtensionURI = IRTPTypes::toHeaderExtensionURI(headerExtensionIter->mURI);
        switch (headerExtensionURI) {
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_TransmissionTimeOffsets:
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_AbsoluteSendTime:
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_3gpp_VideoOrientation:
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_TransportSequenceNumber:
          config.rtp.extensions.push_back(webrtc::RtpExtension(headerExtensionIter->mURI, headerExtensionIter->mID));
          break;
        default:
          break;
        }
        headerExtensionIter++;
      }

      if (mParameters->mRTCP.mReducedSize)
        config.rtp.rtcp_mode = webrtc::RtcpMode::kReducedSize;
      config.decoders.push_back(decoder);
      config.renderer = &mReceiverVideoRenderer;

      mReceiveStream = rtc::scoped_ptr<webrtc::VideoReceiveStream>(
        new webrtc::internal::VideoReceiveStream(
                                                 numCpuCores,
                                                 mCongestionController.get(),
                                                 config,
                                                 NULL,
                                                 mModuleProcessThread.get(),
                                                 mCallStats.get()
                                                 ));

      mReceiveStream->Start();

      notifyPromisesResolve();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoReceiverChannelResource::notifyShutdown()
    {
      // rare race condition that can happen so
      while (mAccessFromNonLockedMethods > 0)
      {
        // NOTE: very temporary lock so should clear itself out fast
        std::this_thread::yield();
      }

      AutoRecursiveLock lock(*this);

      if (mReceiveStream)
        mReceiveStream->Stop();

      mModuleProcessThread->DeRegisterModule(mCallStats.get());
      mModuleProcessThread->Stop();

      mReceiveStream.reset();
      mCongestionController.reset();
      mCallStats.reset();
      mModuleProcessThread.reset();

      notifyPromisesShutdown();
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoSenderChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    RTPMediaEngine::VideoSenderChannelResource::VideoSenderChannelResource(
                                                                           const make_private &priv,
                                                                           IMessageQueuePtr queue,
                                                                           IRTPMediaEngineRegistrationPtr registration,
                                                                           TransportPtr transport,
                                                                           MediaStreamTrackPtr track,
                                                                           ParametersPtr parameters
                                                                           ) :
      ChannelResource(priv, queue, registration),
      mTransport(transport),
      mTrack(track),
      mParameters(parameters)
    {
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::VideoSenderChannelResource::~VideoSenderChannelResource()
    {
      mThisWeak.reset();  // shared pointer to self is no longer valid
    }

    //-------------------------------------------------------------------------
    RTPMediaEngine::VideoSenderChannelResourcePtr RTPMediaEngine::VideoSenderChannelResource::create(
                                                                                                     IRTPMediaEngineRegistrationPtr registration,
                                                                                                     TransportPtr transport,
                                                                                                     MediaStreamTrackPtr track,
                                                                                                     ParametersPtr parameters
                                                                                                     )
    {
      auto pThis = make_shared<VideoSenderChannelResource>(
                                                           make_private{},
                                                           IORTCForInternal::queueBlockingMediaStartStopThread(),
                                                           registration,
                                                           transport,
                                                           track,
                                                           parameters
                                                           );
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoSenderChannelResource::init()
    {
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoSenderChannelResource => IRTPMediaEngineChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoSenderChannelResource => IRTPMediaEngineVideoSenderChannelResource
    #pragma mark

    //-------------------------------------------------------------------------
    bool RTPMediaEngine::VideoSenderChannelResource::handlePacket(const RTCPPacket &packet)
    {
      webrtc::VideoSendStream *stream = NULL;

      {
        AutoRecursiveLock lock(*this);

        stream = mSendStream.get();
        if (NULL == stream) return false;

        ++mAccessFromNonLockedMethods;
      }

      bool result = stream->DeliverRtcp(packet.ptr(), packet.size());
      --mAccessFromNonLockedMethods;
      return result;
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoSenderChannelResource::sendVideoFrame(const webrtc::VideoFrame& videoFrame)
    {
      webrtc::VideoSendStream *stream = NULL;

      {
        AutoRecursiveLock lock(*this);

        stream = mSendStream.get();
        if (NULL == stream) return;

        ++mAccessFromNonLockedMethods;
      }

      stream->Input()->IncomingCapturedFrame(videoFrame);
      --mAccessFromNonLockedMethods;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark RTPMediaEngine::VideoSenderChannelResource => friend RTPMediaEngine
    #pragma mark

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoSenderChannelResource::notifySetup()
    {
      AutoRecursiveLock lock(*this);

      mModuleProcessThread = webrtc::ProcessThread::Create("VideoSenderChannelResourceThread");

      mCallStats = rtc::scoped_ptr<webrtc::CallStats>(new webrtc::CallStats());
      mCongestionController =
        rtc::scoped_ptr<webrtc::CongestionController>(new webrtc::CongestionController(
                                                                                       mModuleProcessThread.get(),
                                                                                       mCallStats.get())
                                                                                       );

      mModuleProcessThread->Start();
      mModuleProcessThread->RegisterModule(mCallStats.get());

      int numCpuCores = webrtc::CpuInfo::DetectNumberOfCores();

      size_t width = 640;
      size_t height = 480;
      int maxFramerate = 15;
      IMediaStreamTrack::SettingsPtr trackSettings = mTrack->getSettings();
      if (trackSettings->mWidth.hasValue())
        width = trackSettings->mWidth.value();
      if (trackSettings->mHeight.hasValue())
        height = trackSettings->mHeight.value();
      if (trackSettings->mFrameRate.hasValue())
        maxFramerate = trackSettings->mFrameRate.value();

      webrtc::VideoSendStream::Config config(mTransport.get());
      webrtc::VideoEncoderConfig encoderConfig;
      std::map<uint32_t, webrtc::RtpState> suspendedSSRCs;

      IRTPTypes::CodecParametersList::iterator codecIter = mParameters->mCodecs.begin();
      while (codecIter != mParameters->mCodecs.end()) {
        auto supportedCodec = IRTPTypes::toSupportedCodec(codecIter->mName);
        if (IRTPTypes::SupportedCodec_VP8 == supportedCodec) {
          webrtc::VideoEncoder* videoEncoder = webrtc::VideoEncoder::Create(webrtc::VideoEncoder::kVp8);
          config.encoder_settings.encoder = videoEncoder;
          config.encoder_settings.payload_name = codecIter->mName;
          config.encoder_settings.payload_type = codecIter->mPayloadType;
          webrtc::VideoStream stream;
          stream.width = width;
          stream.height = height;
          stream.max_framerate = maxFramerate;
          stream.min_bitrate_bps = 30000;
          stream.target_bitrate_bps = 2000000;
          stream.max_bitrate_bps = 2000000;
          stream.max_qp = 56;
          webrtc::VideoCodecVP8 videoCodec = webrtc::VideoEncoder::GetDefaultVp8Settings();
          videoCodec.automaticResizeOn = true;
          videoCodec.denoisingOn = true;
          videoCodec.frameDroppingOn = true;
          encoderConfig.min_transmit_bitrate_bps = 0;
          encoderConfig.content_type = webrtc::VideoEncoderConfig::ContentType::kRealtimeVideo;
          encoderConfig.streams.push_back(stream);
          encoderConfig.encoder_specific_settings = &videoCodec;
          break;
        } else if (IRTPTypes::SupportedCodec_VP9 == supportedCodec) {
          webrtc::VideoEncoder* videoEncoder = webrtc::VideoEncoder::Create(webrtc::VideoEncoder::kVp9);
          config.encoder_settings.encoder = videoEncoder;
          config.encoder_settings.payload_name = codecIter->mName;
          config.encoder_settings.payload_type = codecIter->mPayloadType;
          webrtc::VideoStream stream;
          stream.width = width;
          stream.height = height;
          stream.max_framerate = maxFramerate;
          stream.min_bitrate_bps = 30000;
          stream.target_bitrate_bps = 2000000;
          stream.max_bitrate_bps = 2000000;
          stream.max_qp = 56;
          webrtc::VideoCodecVP9 videoCodec = webrtc::VideoEncoder::GetDefaultVp9Settings();
          videoCodec.frameDroppingOn = true;
          encoderConfig.min_transmit_bitrate_bps = 0;
          encoderConfig.content_type = webrtc::VideoEncoderConfig::ContentType::kRealtimeVideo;
          encoderConfig.streams.push_back(stream);
          encoderConfig.encoder_specific_settings = &videoCodec;
          break;
        } else if (IRTPTypes::SupportedCodec_H264 == supportedCodec) {
          webrtc::VideoEncoder* videoEncoder = webrtc::VideoEncoder::Create(webrtc::VideoEncoder::kH264);
          config.encoder_settings.encoder = videoEncoder;
          config.encoder_settings.payload_name = codecIter->mName;
          config.encoder_settings.payload_type = codecIter->mPayloadType;
          webrtc::VideoStream stream;
          stream.width = width;
          stream.height = height;
          stream.max_framerate = maxFramerate;
          stream.min_bitrate_bps = 30000;
          stream.target_bitrate_bps = 2000000;
          stream.max_bitrate_bps = 2000000;
          stream.max_qp = 56;
          webrtc::VideoCodecH264 videoCodec = webrtc::VideoEncoder::GetDefaultH264Settings();
          videoCodec.frameDroppingOn = true;
          encoderConfig.min_transmit_bitrate_bps = 0;
          encoderConfig.content_type = webrtc::VideoEncoderConfig::ContentType::kRealtimeVideo;
          encoderConfig.streams.push_back(stream);
          encoderConfig.encoder_specific_settings = &videoCodec;
          break;
        }
        IRTPTypes::RTCPFeedbackList::iterator rtcpFeedbackIter = codecIter->mRTCPFeedback.begin();
        while (rtcpFeedbackIter != codecIter->mRTCPFeedback.end()) {
          IRTPTypes::KnownFeedbackTypes feedbackType = IRTPTypes::toKnownFeedbackType(rtcpFeedbackIter->mType);
          IRTPTypes::KnownFeedbackParameters feedbackParameter = IRTPTypes::toKnownFeedbackParameter(rtcpFeedbackIter->mParameter);
          if (IRTPTypes::KnownFeedbackType_NACK == feedbackType && IRTPTypes::KnownFeedbackParameter_Unknown == feedbackParameter) {
            config.rtp.nack.rtp_history_ms = 1000;
          }
          rtcpFeedbackIter++;
        }
        codecIter++;
      }

      IRTPTypes::EncodingParametersList::iterator encodingParamIter = mParameters->mEncodings.begin();
      while (encodingParamIter != mParameters->mEncodings.end()) {
        if (encodingParamIter->mCodecPayloadType == config.encoder_settings.payload_type) {
          config.rtp.ssrcs.push_back(encodingParamIter->mSSRC);
          break;
        }
        encodingParamIter++;
      }
      if (config.rtp.ssrcs.size() == 0)
        config.rtp.ssrcs.push_back(1000);

      IRTPTypes::HeaderExtensionParametersList::iterator headerExtensionIter = mParameters->mHeaderExtensions.begin();
      while (headerExtensionIter != mParameters->mHeaderExtensions.end()) {
        IRTPTypes::HeaderExtensionURIs headerExtensionURI = IRTPTypes::toHeaderExtensionURI(headerExtensionIter->mURI);
        switch (headerExtensionURI) {
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_TransmissionTimeOffsets:
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_AbsoluteSendTime:
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_3gpp_VideoOrientation:
        case IRTPTypes::HeaderExtensionURIs::HeaderExtensionURI_TransportSequenceNumber:
          config.rtp.extensions.push_back(webrtc::RtpExtension(headerExtensionIter->mURI, headerExtensionIter->mID));
          break;
        default:
          break;
        }
        headerExtensionIter++;
      }

      config.rtp.c_name = mParameters->mRTCP.mCName;

      mSendStream = rtc::scoped_ptr<webrtc::VideoSendStream>(
        new webrtc::internal::VideoSendStream(
                                              numCpuCores,
                                              mModuleProcessThread.get(),
                                              mCallStats.get(),
                                              mCongestionController.get(),
                                              config,
                                              encoderConfig,
                                              suspendedSSRCs
                                              ));

      mSendStream->Start();

      notifyPromisesResolve();
    }

    //-------------------------------------------------------------------------
    void RTPMediaEngine::VideoSenderChannelResource::notifyShutdown()
    {
      // rare race condition that can happen so
      while (mAccessFromNonLockedMethods > 0)
      {
        // NOTE: very temporary lock so should clear itself out fast
        std::this_thread::yield();
      }

      AutoRecursiveLock lock(*this);

      if (mSendStream)
        mSendStream->Stop();

      mModuleProcessThread->DeRegisterModule(mCallStats.get());
      mModuleProcessThread->Stop();

      mSendStream.reset();
      mCongestionController.reset();
      mCallStats.reset();
      mModuleProcessThread.reset();

      notifyPromisesShutdown();
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark IRTPMediaEngineFactory
    #pragma mark

    //-------------------------------------------------------------------------
    IRTPMediaEngineFactory &IRTPMediaEngineFactory::singleton()
    {
      return RTPMediaEngineFactory::singleton();
    }

    //-------------------------------------------------------------------------
    RTPMediaEnginePtr IRTPMediaEngineFactory::create(IRTPMediaEngineRegistrationPtr registration)
    {
      if (this) {}
      return internal::RTPMediaEngine::create(registration);
    }

  } // internal namespace
}