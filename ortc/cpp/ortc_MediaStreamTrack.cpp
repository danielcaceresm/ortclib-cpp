/*
 
 Copyright (c) 2013, SMB Phone Inc. / Hookflash Inc.
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

#include <ortc/internal/ortc_MediaStreamTrack.h>
#include <zsLib/Log.h>

namespace ortc { ZS_IMPLEMENT_SUBSYSTEM(ortclib) }

namespace ortc
{
  namespace internal
  {
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    #pragma mark
    #pragma mark IMediaStreamTrackForMediaManager
    #pragma mark
    
    //-------------------------------------------------------------------------
    MediaStreamTrackPtr IMediaStreamTrackForMediaManager::create(IMessageQueuePtr queue, IMediaStreamTrackDelegatePtr delegate)
    {
      MediaStreamTrackPtr pThis(new MediaStreamTrack(queue, delegate));
      pThis->mThisWeak = pThis;
      return pThis;
    }
    
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    #pragma mark
    #pragma mark MediaStreamTrack
    #pragma mark
    
    //-----------------------------------------------------------------------
    MediaStreamTrack::MediaStreamTrack(IMessageQueuePtr queue, IMediaStreamTrackDelegatePtr delegate) :
      MessageQueueAssociator(queue),
      mID(zsLib::createPUID()),
      mDelegate(delegate),
      mError(0),
      mEnabled(false),
      mMuted(false),
      mReadonly(false),
      mRemote(false),
      mReadyState(MediaStreamTrackState_New)
    {
    }
    
    //-----------------------------------------------------------------------
    MediaStreamTrack::~MediaStreamTrack()
    {
    }
    
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    //-----------------------------------------------------------------------
    #pragma mark
    #pragma mark MediaStreamTrack => IMediaStreamTrack
    #pragma mark
    
    String MediaStreamTrack::kind()
    {
      return String();
    }
    
    String MediaStreamTrack::id()
    {
      return String();
    }
    
    String MediaStreamTrack::label()
    {
      return String();
    }
    
    bool MediaStreamTrack::enabled()
    {
      return false;
    }
    
    bool MediaStreamTrack::muted()
    {
      return false;
    }
    
    bool MediaStreamTrack::readonly()
    {
      return false;
    }
    
    bool MediaStreamTrack::remote()
    {
      return false;
    }
    
    MediaStreamTrack::MediaStreamTrackStates MediaStreamTrack::readyState()
    {
      return MediaStreamTrackState_New;
    }
    
    IMediaStreamTrackPtr MediaStreamTrack::clone()
    {
      return IMediaStreamTrackPtr();
    }
    
    void MediaStreamTrack::stop()
    {
      
    }
  }
}