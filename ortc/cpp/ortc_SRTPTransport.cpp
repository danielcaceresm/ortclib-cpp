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

#include <ortc/internal/ortc_SRTPTransport.h>
#include <ortc/internal/ortc_DTLSTransport.h>
#include <ortc/internal/ortc_ORTC.h>
#include <ortc/internal/platform.h>

#include <openpeer/services/ISettings.h>
#include <openpeer/services/IHelper.h>
#include <openpeer/services/IHTTP.h>

#include <zsLib/Stringize.h>
#include <zsLib/Log.h>
#include <zsLib/Numeric.h>
#include <zsLib/XML.h>
#include <zsLib/SafeInt.h>
#include <zsLib/Singleton.h>

#include <cryptopp/integer.h>
#include <cryptopp/sha.h>

#ifdef HAVE_TGMATH_H
#include <tgmath.h>
#else
#include <math.h>
#endif //HAVE_TGMATH_H

//libSRTP
#include "srtp.h"
#include "srtp_priv.h"

#ifdef _DEBUG
#define ASSERT(x) ZS_THROW_BAD_STATE_IF(!(x))
#else
#define ASSERT(x)
#endif //_DEBUG

namespace ortc { ZS_DECLARE_SUBSYSTEM(ortclib) }

#define ORTC_SRTPTRANSPORT_ILLEGAL_MKI_LEGNTH (0xFFFF)


namespace ortc
{
  ZS_DECLARE_TYPEDEF_PTR(openpeer::services::ISettings, UseSettings)
  ZS_DECLARE_TYPEDEF_PTR(openpeer::services::IHelper, UseServicesHelper)
  ZS_DECLARE_TYPEDEF_PTR(openpeer::services::IHTTP, UseHTTP)

  typedef openpeer::services::Hasher<CryptoPP::SHA1> SHA1Hasher;

  typedef CryptoPP::Integer Integer;

  using zsLib::Numeric;

  namespace internal
  {
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark (helpers)
    #pragma mark

#ifndef SRTP_MASTER_KEY_LEN
#define SRTP_MASTER_KEY_LEN (30)
#endif //ndef SRTP_MASTER_KEY_LEN

#define RTP_MINIMUM_PACKET_HEADER_SIZE (12)

    const char CS_AES_CM_128_HMAC_SHA1_80[] = "AES_CM_128_HMAC_SHA1_80";
    const char CS_AES_CM_128_HMAC_SHA1_32[] = "AES_CM_128_HMAC_SHA1_32";

    //-------------------------------------------------------------------------
    static bool isRTCP(const BYTE *data, size_t len) {
      if (len < 2) {
        return false;
      }
      BYTE pt = (data[1] & 0x7F);
      return (63 < pt) && (pt < 96);
    }

    //-------------------------------------------------------------------------
    static size_t toRemainingPercent(
                                     size_t totalPackets,
                                     size_t maxPackets
                                     )
    {
      if (0 == maxPackets) return 0;
      size_t consumed = (totalPackets * 100) / maxPackets;
      if (consumed > 100) return 100;

      size_t remaining = 100 - consumed;
      if (0 != remaining) return remaining;

      if (totalPackets >= maxPackets) return 0;

      // still small amount of lifetime remaining
      return 1;
    }
    
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark ISRTPTransportForSettings
    #pragma mark

    //-------------------------------------------------------------------------
    void ISRTPTransportForSettings::applyDefaults()
    {
//      UseSettings::setUInt(ORTC_SETTING_SRTP_TRANSPORT_WARN_OF_KEY_LIFETIME_EXHAUGSTION_WHEN_REACH_PERCENTAGE_USSED, 90);
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark ISRTPTransportForSecureTransport
    #pragma mark

    //-------------------------------------------------------------------------
    ISRTPTransportForSecureTransport::ParametersPtr ISRTPTransportForSecureTransport::getLocalParameters()
    {
      ParametersPtr params(make_shared<Parameters>());

      {
        CryptoParameters crypto;
        crypto.mCryptoSuite = CS_AES_CM_128_HMAC_SHA1_80;

        KeyParameters key;
        key.mKeyMethod = "inline";
        key.mKeySalt = UseServicesHelper::convertToBase64(*UseServicesHelper::random(SRTP_MASTER_KEY_LEN));
        key.mLifetime = "2^32";
        key.mMKILength = 0;

        crypto.mKeyParams.push_back(key);
        params->mCryptoParams.push_back(crypto);
      }
      {
        CryptoParameters crypto;
        crypto.mCryptoSuite = CS_AES_CM_128_HMAC_SHA1_32;

        KeyParameters key;
        key.mKeyMethod = "inline";
        key.mKeySalt = UseServicesHelper::convertToBase64(*UseServicesHelper::random(SRTP_MASTER_KEY_LEN));
        key.mLifetime = "2^32";
        key.mMKILength = 0;

        crypto.mKeyParams.push_back(key);
        params->mCryptoParams.push_back(crypto);
      }
      return params;
    }

    //-------------------------------------------------------------------------
    ElementPtr ISRTPTransportForSecureTransport::toDebug(ForSecureTransportPtr transport)
    {
      if (!transport) return ElementPtr();
      return ZS_DYNAMIC_PTR_CAST(SRTPTransport, transport)->toDebug();
    }

    //-------------------------------------------------------------------------
    ISRTPTransportForSecureTransport::ForSecureTransportPtr ISRTPTransportForSecureTransport::create(
                                                                                                     ISRTPTransportDelegatePtr delegate,
                                                                                                     UseSecureTransportPtr transport,
                                                                                                     const CryptoParameters &encryptParameters,
                                                                                                     const CryptoParameters &decryptParameters
                                                                                                     )
    {
      return internal::ISRTPTransportFactory::singleton().create(delegate, transport, encryptParameters, decryptParameters);
    }
    

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport
    #pragma mark
    
    //-------------------------------------------------------------------------
    bool SRTPTransport::MKIValueCompare::operator() (const SecureByteBlockPtr &op1, const SecureByteBlockPtr &op2) const
    {
      if (!op1) {
        if (!op2) return false;
        return true;
      } else if (!op2) return false;

      int compare = UseServicesHelper::compare(*op1, *op2);
      return compare < 0;
    }

    //-------------------------------------------------------------------------
    const char *SRTPTransport::toString(Directions state)
    {
      switch (state) {
        case Direction_Encrypt:  return "encrypt";
        case Direction_Decrypt:  return "decrypt";
      }
      return "UNDEFINED";
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPInit
    #pragma mark

    class SRTPInit : public ISingletonManagerDelegate
    {
    protected:
      struct make_private {};

    public:
      //-----------------------------------------------------------------------
      SRTPInit(const make_private &)
      {
        ZS_LOG_BASIC(log("created"))
      }

    protected:
      //-----------------------------------------------------------------------
      void init()
      {
        AutoRecursiveLock lock(mLock);

        int err = srtp_init();
        mInitialized = (err == err_status_ok);
        if (!mInitialized) {
          ZS_LOG_ERROR(Trace, log("Failed to init SRTP") + ZS_PARAM("error", err))
        }
      }

      //-----------------------------------------------------------------------
      static SRTPInitPtr create()
      {
        SRTPInitPtr pThis(make_shared<SRTPInit>(make_private{}));
        pThis->mThisWeak = pThis;
        pThis->init();
        return pThis;
      }

    public:
      //-----------------------------------------------------------------------
      ~SRTPInit()
      {
        mThisWeak.reset();
        ZS_LOG_BASIC(log("destroyed"))
        cancel();
      }

      //-----------------------------------------------------------------------
      static SRTPInitPtr singleton()
      {
        static SingletonLazySharedPtr<SRTPInit> singleton(create());
        SRTPInitPtr result = singleton.singleton();

        static zsLib::SingletonManager::Register registerSingleton("openpeer::ortc::SRTPInit", result);

        if (!result) {
          ZS_LOG_WARNING(Detail, slog("singleton gone"))
        }
        
        return result;
      }

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark SRTPInit => ISingletonManagerDelegate
      #pragma mark

      virtual void notifySingletonCleanup() override
      {
        // ignored
      }

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark SRTPInit => (internal)
      #pragma mark

      //-----------------------------------------------------------------------
      Log::Params log(const char *message) const
      {
        ElementPtr objectEl = Element::create("ortc::SRTPInit");
        UseServicesHelper::debugAppend(objectEl, "id", mID);
        return Log::Params(message, objectEl);
      }

      //-----------------------------------------------------------------------
      static Log::Params slog(const char *message)
      {
        return Log::Params(message, "ortc::SRTPInit");
      }

      //-----------------------------------------------------------------------
      Log::Params debug(const char *message) const
      {
        return Log::Params(message, toDebug());
      }

      //-----------------------------------------------------------------------
      virtual ElementPtr toDebug() const
      {
        AutoRecursiveLock lock(mLock);
        ElementPtr resultEl = Element::create("ortc::SRTPInit");

        UseServicesHelper::debugAppend(resultEl, "id", mID);

        return resultEl;
      }

      //-----------------------------------------------------------------------
      void cancel()
      {
        ZS_LOG_DEBUG(log("cancel called"))

        bool initialized = mInitialized.exchange(false);
        if (!initialized) return;

        int err = srtp_shutdown();
        if (err) {
          ZS_LOG_ERROR(Trace, log("srtp_shutdown failed"))
          return;
        }
      }

    protected:
      //-----------------------------------------------------------------------
      #pragma mark
      #pragma mark SRTPInit => (data)
      #pragma mark

      AutoPUID mID;
      mutable RecursiveLock mLock;
      SRTPInitWeakPtr mThisWeak;

      std::atomic<bool> mInitialized{ false };
    };

    //-------------------------------------------------------------------------
    SRTPTransport::SRTPTransport(
                                 const make_private &,
                                 IMessageQueuePtr queue,
                                 ISRTPTransportDelegatePtr originalDelegate,
                                 UseSecureTransportPtr secureTransport,
                                 const CryptoParameters &encryptParameters,
                                 const CryptoParameters &decryptParameters
                                 ) throw(InvalidParameters) :
      MessageQueueAssociator(queue),
      SharedRecursiveLock(SharedRecursiveLock::create()),
      mSecureTransport(secureTransport),
      mSRTPInit(SRTPInit::singleton())
    {
      ZS_LOG_DETAIL(debug("created"))

      ORTC_THROW_INVALID_STATE_IF(!mSRTPInit)

      mParams[Direction_Encrypt] = encryptParameters;
      mParams[Direction_Decrypt] = decryptParameters;

      if (originalDelegate) {
        mDefaultSubscription = mSubscriptions.subscribe(originalDelegate, IORTCForInternal::queueORTC()); // using ORTC queue and not delegate queue since this is an internal only class
      }

      for (size_t loop = Direction_First; loop <= Direction_Last; ++loop) {

        if (CS_AES_CM_128_HMAC_SHA1_80 == mParams[loop].mCryptoSuite) {
          mMaterial[loop].mAuthenticationTagLength[IICETypes::Component_RTP] = (80/8);
          mMaterial[loop].mAuthenticationTagLength[IICETypes::Component_RTCP] = (80/8);
        } else if (CS_AES_CM_128_HMAC_SHA1_32 == mParams[loop].mCryptoSuite) {
          mMaterial[loop].mAuthenticationTagLength[IICETypes::Component_RTP] = (32/8);
          mMaterial[loop].mAuthenticationTagLength[IICETypes::Component_RTCP] = (80/8);
        } else {
          ZS_LOG_WARNING(Detail, log("crypto suite is not understood") + mParams[loop].toDebug())
          ORTC_THROW_INVALID_PARAMETERS("Crypto suite is not understood: " + mParams[loop].mCryptoSuite)
        }

        size_t mkiLength = ORTC_SRTPTRANSPORT_ILLEGAL_MKI_LEGNTH;

        for (auto iter = mParams[loop].mKeyParams.begin(); iter != mParams[loop].mKeyParams.end(); ++iter) {
          auto keyParam = (*iter);

          if (ORTC_SRTPTRANSPORT_ILLEGAL_MKI_LEGNTH != mkiLength) {
            ORTC_THROW_INVALID_PARAMETERS_IF(mkiLength != keyParam.mMKILength)  // must ALL be the same size
          } else {
            mkiLength = keyParam.mMKILength;
          }

          KeyingMaterialPtr keyingMaterial(make_shared<KeyingMaterial>());

          ORTC_THROW_INVALID_PARAMETERS_IF((keyParam.mMKIValue.hasData()) && (0 == mkiLength))
          ORTC_THROW_INVALID_PARAMETERS_IF((keyParam.mMKIValue.isEmpty()) && (0 != mkiLength))

          keyingMaterial->mOriginalValues = keyParam;
          keyingMaterial->mLifetime = parseLifetime(keyParam.mLifetime);

          if (keyParam.mKeyMethod.hasData()) {
            if (0 != keyParam.mKeyMethod.compareNoCase("inline")) {
              ZS_LOG_WARNING(Detail, log("do not understand non-inline key method"))
              continue;
            }
          }

          keyingMaterial->mKeySalt = UseServicesHelper::convertFromBase64(keyParam.mKeySalt);
          if (!(keyingMaterial->mKeySalt)) {
            ZS_LOG_WARNING(Detail, log("could not extract key salt") + keyParam.toDebug())
            ORTC_THROW_INVALID_PARAMETERS("could not extract key salt:" + keyParam.mKeySalt)
          }

          // NOTE: only supporting this SRTP keying size at this moment
          if (SRTP_MASTER_KEY_LEN != keyingMaterial->mKeySalt->SizeInBytes()) {
            ZS_LOG_WARNING(Detail, log("key is not expected length") + ZS_PARAM("found", keyingMaterial->toDebug()) + ZS_PARAM("expecting", SRTP_MASTER_KEY_LEN) + keyParam.toDebug())
            ORTC_THROW_INVALID_PARAMETERS("key is not expected length:" + keyParam.mKeySalt)
          }

          // If session params are needed, the values can be parsed here. As
          // of the current supported crypto parameters, no session parameters
          // are supported at this time.

          if (!keyingMaterial->mSRTPSession)
          {
              srtp_policy_t policy;
              memset(&policy, 0, sizeof(policy));

              if (CS_AES_CM_128_HMAC_SHA1_80 == mParams[loop].mCryptoSuite) {
                  crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
                  crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
              }
              else if (CS_AES_CM_128_HMAC_SHA1_32 == mParams[loop].mCryptoSuite) {
                  crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);   // rtp is 32,
                  crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);  // rtcp still 80
              }
              else {
                  ZS_LOG_WARNING(Detail, log("crypto suite is not understood") + mParams[loop].toDebug())
                  ORTC_THROW_INVALID_PARAMETERS("Crypto suite is not understood: " + mParams[loop].mCryptoSuite)
              }

              policy.ssrc.type = (loop == Direction_Encrypt ? ssrc_any_outbound : ssrc_any_inbound);
              policy.ssrc.value = 0;
              policy.key = keyingMaterial->mKeySalt->BytePtr();
              
              // TODO(astor) parse window size from WSH session-param
              policy.window_size = 1024;
              policy.allow_repeat_tx = 1;
              // If external authentication option is enabled, supply custom auth module
              // id EXTERNAL_HMAC_SHA1 in the policy structure.
              // We want to set this option only for rtp packets.
              // By default policy structure is initialized to HMAC_SHA1.
              policy.next = NULL;

              int err = srtp_create(&keyingMaterial->mSRTPSession, &policy);
              if (err != err_status_ok) {
                  keyingMaterial->mSRTPSession = NULL;
                  ZS_LOG_ERROR(Debug, log("Failed to create SRTP session, err=") + ZS_PARAM("err=", err))
                  ORTC_THROW_INVALID_PARAMETERS("Failed to create SRTP session")
              }
          }

          if (0 != mkiLength) {
            keyingMaterial->mMKIValue = convertIntegerToBigEndianEncodedBuffer(keyParam.mMKIValue, mkiLength);
            mMaterial[loop].mKeys[keyingMaterial->mMKIValue] = keyingMaterial;
          }

          mMaterial[loop].mKeyList.push_back(keyingMaterial); // when encrypting order matters so need all keys in a list

          mMaterial[loop].mMaxTotalLifetime[IICETypes::Component_RTP] += keyingMaterial->mLifetime;
          mMaterial[loop].mMaxTotalLifetime[IICETypes::Component_RTCP] += keyingMaterial->mLifetime;
        }

        ORTC_THROW_INVALID_PARAMETERS_IF((mMaterial[loop].mKeys.size() > 0) && (0 == mkiLength))
        ORTC_THROW_INVALID_PARAMETERS_IF((mMaterial[loop].mKeys.size() < 1) && (0 != mkiLength))

        mMaterial[loop].mMKILength = mkiLength;
        if (mkiLength > 0) {
          mMaterial[loop].mTempMKIHolder = SecureByteBlockPtr(make_shared<SecureByteBlock>(mkiLength));
        }
      }
    }

    //-------------------------------------------------------------------------
    void SRTPTransport::init()
    {
      //AutoRecursiveLock lock(*this);
      //IWakeDelegateProxy::create(mThisWeak.lock())->onWake();
    }

    //-------------------------------------------------------------------------
    SRTPTransport::~SRTPTransport()
    {
      if (isNoop()) return;

      ZS_LOG_DETAIL(log("destroyed"))
      mThisWeak.reset();

      cancel();
    }

    //-------------------------------------------------------------------------
    SRTPTransportPtr SRTPTransport::convert(ISRTPTransportPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(SRTPTransport, object);
    }

    //-------------------------------------------------------------------------
    SRTPTransportPtr SRTPTransport::convert(ForSettingsPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(SRTPTransport, object);
    }

    //-------------------------------------------------------------------------
    SRTPTransportPtr SRTPTransport::convert(ForSecureTransportPtr object)
    {
      return ZS_DYNAMIC_PTR_CAST(SRTPTransport, object);
    }


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport => ISRTPTransport
    #pragma mark
    
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport => ISRTPTransportForSecureTransport
    #pragma mark
    
    //-------------------------------------------------------------------------
    ElementPtr SRTPTransport::toDebug(SRTPTransportPtr transport)
    {
      if (!transport) return ElementPtr();
      return transport->toDebug();
    }

    //-------------------------------------------------------------------------
    SRTPTransportPtr SRTPTransport::create(
                                           ISRTPTransportDelegatePtr delegate,
                                           UseSecureTransportPtr transport,
                                           const CryptoParameters &encryptParameters,
                                           const CryptoParameters &decryptParameters
                                           ) throw(InvalidParameters)
    {
      SRTPTransportPtr pThis(make_shared<SRTPTransport>(make_private {}, IORTCForInternal::queueORTC(), delegate, transport, encryptParameters, decryptParameters));
      pThis->mThisWeak = pThis;
      pThis->init();
      return pThis;
    }

    //-------------------------------------------------------------------------
    ISRTPTransportSubscriptionPtr SRTPTransport::subscribe(ISRTPTransportDelegatePtr originalDelegate)
    {
      ZS_LOG_DETAIL(log("subscribing to transport state"))

      AutoRecursiveLock lock(*this);
      if (!originalDelegate) return mDefaultSubscription;

      ISRTPTransportSubscriptionPtr subscription = mSubscriptions.subscribe(originalDelegate, IORTCForInternal::queueDelegate());

      ISRTPTransportDelegatePtr delegate = mSubscriptions.delegate(subscription, true);

      if (delegate) {
        SRTPTransportPtr pThis = mThisWeak.lock();

        if ((100 != mLastRemainingLeastKeyPercentageReported) ||
            (100 != mLastRemainingOverallPercentageReported)) {
          delegate->onSRTPTransportLifetimeRemaining(pThis, mLastRemainingLeastKeyPercentageReported, mLastRemainingOverallPercentageReported);
        }
      }

      return subscription;
    }

    //-------------------------------------------------------------------------
    bool SRTPTransport::handleReceivedPacket(
                                             IICETypes::Components viaTransport,
                                             const BYTE *buffer,
                                             size_t bufferLengthInBytes
                                             )
    {
      UseSecureTransportPtr transport;
      SecureByteBlockPtr decryptedBuffer;
      IICETypes::Components component = (isRTCP(buffer, bufferLengthInBytes) ? IICETypes::Component_RTCP : IICETypes::Component_RTP);

      size_t popSize = 0;
      enum UsedKeys {
        UsedKey_First,

        UsedKey_Current = UsedKey_First,
        UsedKey_Next,
        UsedKey_Old,

        UsedKey_Last = UsedKey_Old
      };

      // WARNING: do NOT modify contents of what pointer is pointing to outside of a lock (shouldn't need to change contents anyway)
      KeyingMaterialPtr usedKeys[UsedKey_Last + 1];
      UsedKeys decryptedWithKey {UsedKey_First};

      DirectionMaterial &material = mMaterial[Direction_Decrypt]; // WARNING: only some values are accessible outside a lock

      const BYTE *packetMKI {NULL};

      //size_t authenticationTagLength = material.mAuthenticationTagLength[component];
      //lbojan fix for SRTCP packet lenght
      size_t authenticationTagLength{ 0 };// = material.mAuthenticationTagLength[packetType];
      component == IICETypes::Component_RTP ? (authenticationTagLength = material.mAuthenticationTagLength[component]) : (authenticationTagLength = material.mAuthenticationTagLength[component] + 4);

      if (material.mMKILength > 0) {
        if (bufferLengthInBytes < (RTP_MINIMUM_PACKET_HEADER_SIZE + material.mMKILength + authenticationTagLength)) {
          ZS_LOG_WARNING(Debug, log("packet length is wrong (thus discarding)") + ZS_PARAM("buffer length in bytes", bufferLengthInBytes))
          return false;
        }
        packetMKI = &(buffer[bufferLengthInBytes - authenticationTagLength - material.mMKILength]);
      }

      // NOTE: *** WARNING ***
      // DO NOT TRUST THE INCOMING PACKET. Assume every size, index and
      // value inside the incoming packet is malicious. Thus double check
      // indexes, positions, length are within range of the packet BEFORE
      // extracting or continuing. If anything looks wrong then immediately
      // log a warning and abort out of the decoding process IMMEDIATELY.

      {
        AutoRecursiveLock lock(*this);

        if (0 == mLastRemainingOverallPercentageReported) {
          ZS_LOG_WARNING(Detail, log("cannot decrypt packet as packet lifetime is exhausted (and continuing to decrypt would violate security principles)"))
          return false;
        }

        transport = mSecureTransport.lock();
        if (!transport) {
          ZS_LOG_WARNING(Debug, log("nowhere to send packet as secure transport is gone"))
          return false;
        }

        if (0 != material.mMKILength) {
          if (NULL == packetMKI) {
            ZS_LOG_WARNING(Debug, log("packet mki value was not present (thus aborting decryption)") + ZS_PARAM("buffer length in bytes", bufferLengthInBytes))
            return false;
          }

          ASSERT(((bool)material.mTempMKIHolder)) // must be present
          ASSERT(material.mMKILength == material.mTempMKIHolder->SizeInBytes()) // must be identical

          memcpy(material.mTempMKIHolder->BytePtr(), packetMKI, material.mTempMKIHolder->SizeInBytes());

          auto found = material.mKeys.find(material.mTempMKIHolder);
          if (found == material.mKeys.end()) {
            ZS_LOG_WARNING(Debug, log("no key was found with packet's MKI value") + ZS_PARAM("mki value", UseServicesHelper::convertToHex(*(material.mTempMKIHolder))))
            return false;
          }

          usedKeys[UsedKey_Current] = (*found).second;
        } else {
          if (material.mKeyList.size() < 1) {
            ZS_LOG_WARNING(Debug, log("keying material is exhausted"))
            return false;
          }

          usedKeys[UsedKey_Old] = material.mOldKey;
          usedKeys[UsedKey_Current] = material.mKeyList.front();
          if (material.mKeyList.size() > 1) {
              usedKeys[UsedKey_Next] = *(++(material.mKeyList.begin())); // only set if there is a next key
          }

          popSize = material.mKeyList.size();
        }

        // NOTE: oldKey and nextKey might be null if there is no older key or
        // next key. However, currentKey must be valid.

        ASSERT(((bool)usedKeys[UsedKey_Current]))

        if (!usedKeys[UsedKey_Current]) {
          ZS_LOG_ERROR(Debug, log("no keying material found to decrypt packet") + ZS_PARAM("buffer length in bytes", bufferLengthInBytes))
          return false;
        }
      }

      ASSERT(((bool)transport))

      if (material.mMKILength > 0) {
        // As part of the decryption process, the MKI value must be stripped from
        // the packet. This is done by selectively copying from the source packet
        // to the decryptedBuffer (which is not yet decrypted).
        decryptedBuffer = SecureByteBlockPtr(make_shared<SecureByteBlock>(bufferLengthInBytes - material.mMKILength));

        size_t headerAndPayloadSize = bufferLengthInBytes - authenticationTagLength - material.mMKILength;

        // first copy the RTP header and encrypted payload
        memcpy(decryptedBuffer->BytePtr(), buffer, headerAndPayloadSize);

        BYTE *destAuthTag = &((decryptedBuffer->BytePtr())[headerAndPayloadSize]);
        const BYTE *sourceAuthTag = &(buffer[headerAndPayloadSize + material.mMKILength]);

        memcpy(destAuthTag, sourceAuthTag, authenticationTagLength);
      } else {
        // nothing fancy here, just copy the source packet into the decrypted
        // buffer and prepare for decryption
        decryptedBuffer = SecureByteBlockPtr(make_shared<SecureByteBlock>(buffer, bufferLengthInBytes));
      }


      // NOTE: The decryptedBuffer now includes the RTP header, payload and
      // authentication tag without the MKI value in the packet.
      
      bool foundKey {false};
      int out_len {};
      for (size_t loop = UsedKey_First; loop <= UsedKey_Last; ++loop)
      {
        if (!((bool)(usedKeys[loop]))) continue;

        out_len = SafeInt<decltype(out_len)>(decryptedBuffer->SizeInBytes());

        // scope: lock the keying material with its own individual lock
        {
          AutoLock lock(usedKeys[loop]->mSRTPSessionLock);
          int err = (component == IICETypes::Component_RTP ? srtp_unprotect(usedKeys[loop]->mSRTPSession, decryptedBuffer->BytePtr(), &out_len) :
                                                             srtp_unprotect_rtcp(usedKeys[loop]->mSRTPSession, decryptedBuffer->BytePtr(), &out_len));
          if (err != err_status_ok) {
            ZS_LOG_WARNING(Trace, log("cannot use current keying material, trying with next key") + usedKeys[loop]->toDebug())
            continue;
          }

          //uint32 ssrc;
          //if (GetRtpSsrc(p, in_len, &ssrc)) {
          //    srtp_stat_->AddUnprotectRtpResult(ssrc, err);
          //}
        }

        foundKey = true;
        decryptedWithKey = static_cast<UsedKeys>(loop);
        break;
      }

      if (!foundKey)
      {
        ZS_LOG_WARNING(Trace, log("cannot decrypt packet with any key (thus discarding packet)"))
        return false;
      }

      ASSERT(((bool)usedKeys[decryptedWithKey]));

      // need to update the usage of the key (depending on which key was acutally used for decrypting)
      {
        AutoRecursiveLock lock(*this);

        if (usedKeys[decryptedWithKey]->mTotalPackets[component] + 1 > usedKeys[decryptedWithKey]->mLifetime) {
          ZS_LOG_WARNING(Debug, log("cannot use keying material as it's lifetime is exhausted") + usedKeys[decryptedWithKey]->toDebug())
          return false;
        }

        updateTotalPackets(Direction_Decrypt, component, usedKeys[decryptedWithKey]);

        if (decryptedWithKey == UsedKey_Next) {
          // double check this key has not already been popped off by another thread
          if (popSize == material.mKeyList.size()) {
            material.mKeyList.pop_front();  // the current key is disposed
            material.mOldKey = usedKeys[UsedKey_Current];  // remember the current key as the old key
          }
        }
      }

      ASSERT(((bool)decryptedBuffer))
      ASSERT(out_len > 0)

      ASSERT(out_len <= SafeInt<decltype(out_len)>(decryptedBuffer->SizeInBytes()))

      ZS_LOG_INSANE(log("forwarding packet to secure transport") + ZS_PARAM("via", IICETypes::toString(viaTransport)) + ZS_PARAM("component", IICETypes::toString(component)) + ZS_PARAM("buffer length in bytes", decryptedBuffer->SizeInBytes()))

      return transport->handleReceivedDecryptedPacket(viaTransport, component, decryptedBuffer->BytePtr(), SafeInt<decltype(out_len)>(out_len));
    }

    //-------------------------------------------------------------------------
    bool SRTPTransport::sendPacket(
                                   IICETypes::Components sendOverICETransport,
                                   IICETypes::Components packetType,  // is packet RTP or RTCP
                                   const BYTE *buffer,
                                   size_t bufferLengthInBytes
                                   )
    {
      UseSecureTransportPtr transport;
      KeyingMaterialPtr keyingMaterial;

      SecureByteBlockPtr encryptedBuffer;

      DirectionMaterial &material = mMaterial[Direction_Encrypt]; // WARNING: only some values are accessible outside a lock


      //lbojan fix for SRTCP packet lenght
      size_t authenticationTagLength  {0};// = material.mAuthenticationTagLength[packetType];
      packetType == IICETypes::Component_RTP ? (authenticationTagLength = material.mAuthenticationTagLength[packetType]) : (authenticationTagLength = material.mAuthenticationTagLength[packetType] + 4);

      {
        AutoRecursiveLock lock(*this);

        if (0 == mLastRemainingOverallPercentageReported) {
          ZS_LOG_WARNING(Detail, log("cannot encrypt packet as packet lifetime is exhausted"))
          return false;
        }

        transport = mSecureTransport.lock();
        if (!transport) {
          ZS_LOG_WARNING(Debug, log("nowhere to send packet as secure transport is gone"))
          return false;
        }

        while (true) {
          if (material.mKeyList.size() < 1) {
            ZS_LOG_WARNING(Debug, log("no more keying material is present (all lifetimes are exhausted)") + material.toDebug())
            return false;
          }

          keyingMaterial = material.mKeyList.front();

          ASSERT(((bool)keyingMaterial))

          if (keyingMaterial->mTotalPackets[packetType] + 1 > keyingMaterial->mLifetime) {
            ZS_LOG_WARNING(Debug, log("cannot use keying material as it's lifetime is exhausted") + keyingMaterial->toDebug())
            material.mKeyList.pop_front();
            continue; // try another key
          }

          break;
        }

        updateTotalPackets(Direction_Encrypt, packetType, keyingMaterial);
      }

      // Encrypted buffer must include enough room for the full packet and the
      // MKI and authentication tag.
      encryptedBuffer = SecureByteBlockPtr(make_shared<SecureByteBlock>(bufferLengthInBytes + authenticationTagLength + material.mMKILength));

      memcpy(encryptedBuffer->BytePtr(), buffer, bufferLengthInBytes);

      // lib srtp does not understand MKI thus we need to tell it the
      // space available for the packet without including the additional MKI
      // field...
      //size_t libSRTPMaxLength = bufferLengthInBytes + authenticationTagLength;

      int out_len {static_cast<int>(bufferLengthInBytes)};
      int err {};

      // scope: lock the keying material with its own individual lock
      {
        AutoLock lock(keyingMaterial->mSRTPSessionLock);
        err = (packetType == IICETypes::Component_RTP ? srtp_protect(keyingMaterial->mSRTPSession, encryptedBuffer->BytePtr(), &out_len) :
                                                        srtp_protect_rtcp(keyingMaterial->mSRTPSession, encryptedBuffer->BytePtr(), &out_len));

        //uint32 ssrc;
        //if (GetRtpSsrc(p, in_len, &ssrc)) {
        //    srtp_stat_->AddProtectRtpResult(ssrc, err);
        //}
      }

      if (err != err_status_ok) {
        ZS_LOG_WARNING(Debug, log("cannot use current keying material for encryption") + keyingMaterial->toDebug())
        return false;
      }

      if (material.mMKILength > 0) {
        // Need to make room for the MKI by moving the authentication tag
        // after the spot where the MKI is to be inserted. Once moved then
        // the MKI value from the keying material can be copied into the
        // packet's MKI location.
        const BYTE *sourceAuthentication = &(encryptedBuffer->BytePtr()[bufferLengthInBytes]);
        BYTE *destAuthentication = &(encryptedBuffer->BytePtr()[bufferLengthInBytes + material.mMKILength]);
        BYTE *packetMKI = &(encryptedBuffer->BytePtr()[bufferLengthInBytes]);

        memmove(destAuthentication, sourceAuthentication, authenticationTagLength);   // must use a memmove not a memcpy incase the source/dest buffers overlap
        memcpy(packetMKI, keyingMaterial->mMKIValue->BytePtr(), material.mMKILength);
      }


      ASSERT(((bool)transport))
      ASSERT(((bool)encryptedBuffer))

      ASSERT(out_len <= SafeInt<decltype(out_len)>(encryptedBuffer->SizeInBytes()))

      // do NOT call this method from within a lock
      return transport->sendEncryptedPacket(sendOverICETransport, packetType, encryptedBuffer->BytePtr(), encryptedBuffer->SizeInBytes());
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport => IWakeDelegate
    #pragma mark

    //-------------------------------------------------------------------------
    void SRTPTransport::onWake()
    {
      // NOT USED
      // ZS_LOG_DEBUG(log("wake"))
      // AutoRecursiveLock lock(*this);
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport => ITimerDelegate
    #pragma mark

    //-------------------------------------------------------------------------
    void SRTPTransport::onTimer(TimerPtr timer)
    {
      // NOT USED
      // ZS_LOG_DEBUG(log("timer") + ZS_PARAM("timer id", timer->getID()))
      // AutoRecursiveLock lock(*this);
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport => ISRTPTransportAsyncDelegate
    #pragma mark


    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport => (internal)
    #pragma mark

    //-------------------------------------------------------------------------
    Log::Params SRTPTransport::log(const char *message) const
    {
      ElementPtr objectEl = Element::create("ortc::SRTPTransport");
      UseServicesHelper::debugAppend(objectEl, "id", mID);
      return Log::Params(message, objectEl);
    }

    //-------------------------------------------------------------------------
    Log::Params SRTPTransport::slog(const char *message)
    {
      ElementPtr objectEl = Element::create("ortc::SRTPTransport");
      return Log::Params(message, objectEl);
    }

    //-------------------------------------------------------------------------
    Log::Params SRTPTransport::debug(const char *message) const
    {
      return Log::Params(message, toDebug());
    }

    //-------------------------------------------------------------------------
    ElementPtr SRTPTransport::toDebug() const
    {
      AutoRecursiveLock lock(*this);

      ElementPtr resultEl = Element::create("ortc::SRTPTransport");

      UseServicesHelper::debugAppend(resultEl, "id", mID);

      UseServicesHelper::debugAppend(resultEl, "subscribers", mSubscriptions.size());
      UseServicesHelper::debugAppend(resultEl, "default subscription", (bool)mDefaultSubscription);

      UseSecureTransportPtr secureTransport = mSecureTransport.lock();
      UseServicesHelper::debugAppend(resultEl, "secure transport", secureTransport ? secureTransport->getID() : 0);

      UseServicesHelper::debugAppend(resultEl, "encrypt params", mParams[Direction_Encrypt].toDebug());
      UseServicesHelper::debugAppend(resultEl, "decrypt params", mParams[Direction_Decrypt].toDebug());

      UseServicesHelper::debugAppend(resultEl, "last remaining least key percentage reported", mLastRemainingLeastKeyPercentageReported);
      UseServicesHelper::debugAppend(resultEl, "last remaining overall percentage reported", mLastRemainingOverallPercentageReported);

      for (size_t loopDirection = Direction_First; loopDirection != Direction_Last; ++loopDirection) {
        UseServicesHelper::debugAppend(resultEl, toString((Directions)loopDirection), mMaterial[loopDirection].toDebug());
      }

      return resultEl;
    }

    //-------------------------------------------------------------------------
    void SRTPTransport::cancel()
    {
      //.......................................................................
      mSRTPInit.reset();
      // final cleanup

      mSubscriptions.clear();

      if (mDefaultSubscription) {
        mDefaultSubscription->cancel();
        mDefaultSubscription.reset();
      }
    }

    //-------------------------------------------------------------------------
    void SRTPTransport::updateTotalPackets(
                                           Directions direction,
                                           IICETypes::Components component,
                                           KeyingMaterialPtr &keyingMaterial
                                           )
    {
      size_t &totalKeyPackets = (keyingMaterial->mTotalPackets[component]);
      size_t lifetimeKey = (keyingMaterial->mLifetime);
      size_t &totalDirectionPackets = (mMaterial[direction].mTotalPackets[component]);
      size_t lifetimeDirection = (mMaterial[direction].mMaxTotalLifetime[component]);

      ++(totalKeyPackets);
      ++(totalDirectionPackets);

      size_t remainingForKey = toRemainingPercent(totalKeyPackets, lifetimeKey);
      size_t remainingDirection = toRemainingPercent(totalDirectionPackets, lifetimeDirection);

      bool changed = false;

      if (remainingForKey < mLastRemainingLeastKeyPercentageReported) {
        mLastRemainingLeastKeyPercentageReported = remainingForKey;
        changed = true;
      }

      if (remainingDirection < mLastRemainingOverallPercentageReported) {
        mLastRemainingOverallPercentageReported = remainingDirection;
        changed = true;
      }

      if (!changed) return;

      auto pThis = mThisWeak.lock();
      if (pThis) {
        ZS_LOG_TRACE(log("reporting remaining percentages") + ZS_PARAM("least for key", remainingForKey) + ZS_PARAM("overall", mLastRemainingOverallPercentageReported))
        mSubscriptions.delegate()->onSRTPTransportLifetimeRemaining(pThis, mLastRemainingLeastKeyPercentageReported, mLastRemainingOverallPercentageReported);
      }
    }

    //-------------------------------------------------------------------------
    size_t SRTPTransport::parseLifetime(const String &lifetime) throw(InvalidParameters)
    {
      ORTC_THROW_INVALID_PARAMETERS_IF(lifetime.isEmpty())

      UseServicesHelper::SplitMap splitValues;

      UseServicesHelper::split(lifetime, splitValues, '^');

      if (splitValues.size() < 2) {
        try {
          return Numeric<size_t>(lifetime);
        } catch(Numeric<size_t>::ValueOutOfRange &) {
          ZS_LOG_WARNING(Detail, slog("lifetime value out of range") + ZS_PARAM("lifetime", lifetime))
          ORTC_THROW_INVALID_PARAMETERS("unable to parse lifetime: " + lifetime)
        }
      }

      if (splitValues.size() > 2) {
        ZS_LOG_ERROR(Detail, slog("unable to parse lifetime") + ZS_PARAM("lifetime", lifetime))
        ORTC_THROW_INVALID_PARAMETERS("unable to parse lifetime: " + lifetime)
      }

      size_t base {};
      size_t exponent {};

      try {
        base = Numeric<size_t>(splitValues[0]);
        exponent = Numeric<size_t>(splitValues[1]);
      } catch (Numeric<size_t>::ValueOutOfRange &) {
        ZS_LOG_ERROR(Detail, slog("unable to parse lifetime") + ZS_PARAM("lifetime", lifetime))
        ORTC_THROW_INVALID_PARAMETERS("unable to parse lifetime:" + lifetime)
      }

      try {
        return
#ifndef HAVE_TGMATH_H
          static_cast<size_t>(
#endif //HAVE_TGMATH_H
        pow(base, exponent)
#ifndef HAVE_TGMATH_H
        )
#endif //HAVE_TGMATH_H
        ;
      }
      catch (...) {
        ZS_LOG_ERROR(Detail, slog("unable to parse lifetime") + ZS_PARAM("lifetime", lifetime))
        ORTC_THROW_INVALID_PARAMETERS("unable to parse lifetime:" + lifetime)
      }

      ASSERT(false) // should never hit this point
      return 0;
    }

    //-------------------------------------------------------------------------
    SecureByteBlockPtr SRTPTransport::convertIntegerToBigEndianEncodedBuffer(
                                                                             const String &base10Value,
                                                                             size_t maxByteLength
                                                                             ) throw(InvalidParameters)
    {
      SecureByteBlockPtr output(make_shared<SecureByteBlock>(maxByteLength));

      if (base10Value.isEmpty()) return output;

      try {
        Integer value(base10Value); // convert from base 10 into big number class

        size_t minSizeNeeded = value.MinEncodedSize();
        ORTC_THROW_INVALID_PARAMETERS_IF(minSizeNeeded > maxByteLength)

        // this will encode in big endian and pad with most significant "0"
        // values as needed
        value.Encode(output->BytePtr(), output->SizeInBytes());

      } catch(...) {
        ORTC_THROW_INVALID_PARAMETERS("unable to convert integer: " + base10Value)
      }

      return output;
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport::KeyingMaterial
    #pragma mark

    //-------------------------------------------------------------------------
    ElementPtr SRTPTransport::KeyingMaterial::toDebug() const
    {
      ElementPtr resultEl = Element::create("ortc::SRTPTransport::KeyingMaterial");

      UseServicesHelper::debugAppend(resultEl, mOriginalValues.toDebug());
      UseServicesHelper::debugAppend(resultEl, "mki (hex)", mMKIValue ? UseServicesHelper::convertToHex(*mMKIValue) : String());

      UseServicesHelper::debugAppend(resultEl, "lifetime", mLifetime);

      for (size_t loopComponent = IICETypes::Component_First; loopComponent <= IICETypes::Component_Last; ++loopComponent) {
        const char *message = "UNDEFINED";
        switch (((IICETypes::Components) loopComponent)) {
          case IICETypes::Component_RTP:    message = "total RTP packets"; break;
          case IICETypes::Component_RTCP:   message = "total RTCP packets"; break;
        }
        UseServicesHelper::debugAppend(resultEl, message, mTotalPackets[IICETypes::Component_RTP]);
      }

      UseServicesHelper::debugAppend(resultEl, "key salt", mKeySalt ? UseServicesHelper::convertToHex(*mKeySalt) : String());

      UseServicesHelper::debugAppend(resultEl, "srtp session", (PTRNUMBER)mSRTPSession);

      return resultEl;
    }

    //-------------------------------------------------------------------------
    String SRTPTransport::KeyingMaterial::hash() const
    {
      SHA1Hasher hasher;

      hasher.update("ortc:SRTPTransport::KeyingMaterial:");

      hasher.update(mOriginalValues.hash());
      hasher.update(":");
      hasher.update(mMKIValue ? UseServicesHelper::convertToHex(*mMKIValue) : String());

      hasher.update(":");
      hasher.update(mLifetime);

      for (size_t loopComponent = IICETypes::Component_First; loopComponent <= IICETypes::Component_Last; ++loopComponent) {
        hasher.update(":");
        hasher.update(mTotalPackets[loopComponent]);
      }

      return hasher.final();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark SRTPTransport::DirectionMaterial
    #pragma mark

    //-------------------------------------------------------------------------
    ElementPtr SRTPTransport::DirectionMaterial::toDebug() const
    {
      ElementPtr resultEl = Element::create("ortc::SRTPTransport::DirectionMaterial");

      UseServicesHelper::debugAppend(resultEl, "mki length", mMKILength);

      UseServicesHelper::debugAppend(resultEl, "temp mki holder (hex)", mTempMKIHolder ? UseServicesHelper::convertToHex(*mTempMKIHolder) : String());

      for (auto iter = mKeys.begin(); iter != mKeys.end(); ++iter)
      {
        auto keyingMaterial = (*iter).second;
        UseServicesHelper::debugAppend(resultEl, keyingMaterial->toDebug());
      }

      for (size_t loop = IICETypes::Component_First; loop <= IICETypes::Component_Last; ++loop) {
        const char *message = "max total lifetime (UNKNOWN)";
        switch (loop) {
          case IICETypes::Component_RTP:   message = "max total lifetime (RTP)"; break;
          case IICETypes::Component_RTCP:  message = "max total lifetime (RTCP)"; break;
        }
        UseServicesHelper::debugAppend(resultEl, message, mMaxTotalLifetime[loop]);
      }

      return resultEl;
    }

    //-------------------------------------------------------------------------
    String SRTPTransport::DirectionMaterial::hash() const
    {
      SHA1Hasher hasher;

      hasher.update("ortc:SRTPTransport::DirectionMaterial:");

      hasher.update(mMKILength);
      hasher.update(":");
      hasher.update(mTempMKIHolder ? string(mTempMKIHolder->SizeInBytes()) : "0");  // do not hex encode because value is bogus temporary (but size must be fixed)

      for (auto iter = mKeys.begin(); iter != mKeys.end(); ++iter)
      {
        auto keyingMaterial = (*iter).second;

        auto hash = keyingMaterial->hash();

        hasher.update(":");
        hasher.update(hash);
      }

      hasher.update(":");
      for (size_t loop = IICETypes::Component_First; loop <= IICETypes::Component_Last; ++loop) {
        hasher.update(":");
        hasher.update(mMaxTotalLifetime[loop]);
      }

      return hasher.final();
    }

    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    #pragma mark
    #pragma mark ISRTPTransportFactory
    #pragma mark

    //-------------------------------------------------------------------------
    ISRTPTransportFactory &ISRTPTransportFactory::singleton()
    {
      return SRTPTransportFactory::singleton();
    }

    //-------------------------------------------------------------------------
    SRTPTransportPtr ISRTPTransportFactory::create(
                                                   ISRTPTransportDelegatePtr delegate,
                                                   UseSecureTransportPtr transport,
                                                   const CryptoParameters &encryptParameters,
                                                   const CryptoParameters &decryptParameters
                                                   )
    {
      if (this) {}
      return internal::SRTPTransport::create(delegate, transport, encryptParameters, decryptParameters);
    }


  } // internal namespace

}