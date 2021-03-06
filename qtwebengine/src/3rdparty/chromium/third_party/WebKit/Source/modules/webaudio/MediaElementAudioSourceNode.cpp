/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include "modules/webaudio/MediaElementAudioSourceNode.h"

#include "core/dom/ExecutionContextTask.h"
#include "core/html/HTMLMediaElement.h"
#include "core/inspector/ConsoleMessage.h"
#include "modules/webaudio/AudioNodeOutput.h"
#include "modules/webaudio/BaseAudioContext.h"
#include "modules/webaudio/MediaElementAudioSourceOptions.h"
#include "platform/audio/AudioUtilities.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "wtf/Locker.h"
#include "wtf/PtrUtil.h"

namespace blink {

MediaElementAudioSourceHandler::MediaElementAudioSourceHandler(
    AudioNode& node,
    HTMLMediaElement& mediaElement)
    : AudioHandler(NodeTypeMediaElementAudioSource,
                   node,
                   node.context()->sampleRate()),
      m_mediaElement(mediaElement),
      m_sourceNumberOfChannels(0),
      m_sourceSampleRate(0),
      is_origin_tainted_(false) {
  DCHECK(isMainThread());
  // Default to stereo. This could change depending on what the media element
  // .src is set to.
  addOutput(2);

  initialize();
}

PassRefPtr<MediaElementAudioSourceHandler>
MediaElementAudioSourceHandler::create(AudioNode& node,
                                       HTMLMediaElement& mediaElement) {
  return adoptRef(new MediaElementAudioSourceHandler(node, mediaElement));
}

MediaElementAudioSourceHandler::~MediaElementAudioSourceHandler() {
  uninitialize();
}

void MediaElementAudioSourceHandler::dispose() {
  m_mediaElement->setAudioSourceNode(nullptr);
  AudioHandler::dispose();
}

void MediaElementAudioSourceHandler::setFormat(size_t numberOfChannels,
                                               float sourceSampleRate) {
  bool is_tainted = wouldTaintOrigin();

  if (is_tainted) {
    printCORSMessage(mediaElement()->currentSrc().getString());
  }
					       
  if (numberOfChannels != m_sourceNumberOfChannels ||
      sourceSampleRate != m_sourceSampleRate) {
    if (!numberOfChannels ||
        numberOfChannels > BaseAudioContext::maxNumberOfChannels() ||
        !AudioUtilities::isValidAudioBufferSampleRate(sourceSampleRate)) {

      // process() will generate silence for these uninitialized values.
      DLOG(ERROR) << "setFormat(" << numberOfChannels << ", "
                  << sourceSampleRate << ") - unhandled format change";
      // Synchronize with process().
      Locker<MediaElementAudioSourceHandler> locker(*this);
      m_sourceNumberOfChannels = 0;
      m_sourceSampleRate = 0;
      is_origin_tainted_ = is_tainted;
      return;
    }

    // Synchronize with process() to protect |source_number_of_channels_|,
    // |source_sample_rate_|, |multi_channel_resampler_|. and
    // |is_origin_tainted_|.
    Locker<MediaElementAudioSourceHandler> locker(*this);

    is_origin_tainted_ = is_tainted;
    m_sourceNumberOfChannels = numberOfChannels;
    m_sourceSampleRate = sourceSampleRate;

    if (sourceSampleRate != sampleRate()) {
      double scaleFactor = sourceSampleRate / sampleRate();
      m_multiChannelResampler =
          makeUnique<MultiChannelResampler>(scaleFactor, numberOfChannels);
    } else {
      // Bypass resampling.
      m_multiChannelResampler.reset();
    }

    {
      // The context must be locked when changing the number of output channels.
      BaseAudioContext::AutoLocker contextLocker(context());

      // Do any necesssary re-configuration to the output's number of channels.
      output(0).setNumberOfChannels(numberOfChannels);
    }
  }
}

bool MediaElementAudioSourceHandler::wouldTaintOrigin() {
  // If we're cross-origin and allowed access vie CORS, we're not tainted.
  if (mediaElement()->webMediaPlayer()->didPassCORSAccessCheck()) {
    return false;
  }

  // Handles the case where the url is a redirect to another site that we're not
  // allowed to access.
  if (!mediaElement()->hasSingleSecurityOrigin()) {
    return true;
  }

  // Test to see if the current media URL taint the origin of the audio context?
  return context()->WouldTaintOrigin(mediaElement()->currentSrc());
}

void MediaElementAudioSourceHandler::printCORSMessage(const String& message) {
  if (context()->getExecutionContext()) {
    context()->getExecutionContext()->addConsoleMessage(
        ConsoleMessage::create(SecurityMessageSource, InfoMessageLevel,
                               "MediaElementAudioSource outputs zeroes due to "
                               "CORS access restrictions for " +
                                   message));
  }
}

void MediaElementAudioSourceHandler::process(size_t numberOfFrames) {
  AudioBus* outputBus = output(0).bus();

  // Use a tryLock() to avoid contention in the real-time audio thread.
  // If we fail to acquire the lock then the HTMLMediaElement must be in the
  // middle of reconfiguring its playback engine, so we output silence in this
  // case.
  MutexTryLocker tryLocker(m_processLock);
  if (tryLocker.locked()) {
    if (!mediaElement() || !m_sourceNumberOfChannels || !m_sourceSampleRate) {
      outputBus->zero();
      return;
    }
    AudioSourceProvider& provider = mediaElement()->getAudioSourceProvider();
    // Grab data from the provider so that the element continues to make
    // progress, even if we're going to output silence anyway.
    if (m_multiChannelResampler.get()) {
      DCHECK_NE(m_sourceSampleRate, sampleRate());
      m_multiChannelResampler->process(&provider, outputBus, numberOfFrames);
    } else {
      // Bypass the resampler completely if the source is at the context's
      // sample-rate.
      DCHECK_EQ(m_sourceSampleRate, sampleRate());
      provider.provideInput(outputBus, numberOfFrames);
    }
    // Output silence if we don't have access to the element.
    if (is_origin_tainted_) {
      outputBus->zero();
    }
  } else {
    // We failed to acquire the lock.
    outputBus->zero();
  }
}

void MediaElementAudioSourceHandler::lock() {
  m_processLock.lock();
}

void MediaElementAudioSourceHandler::unlock() {
  m_processLock.unlock();
}

// ----------------------------------------------------------------

MediaElementAudioSourceNode::MediaElementAudioSourceNode(
    BaseAudioContext& context,
    HTMLMediaElement& mediaElement)
    : AudioSourceNode(context) {
  setHandler(MediaElementAudioSourceHandler::create(*this, mediaElement));
}

MediaElementAudioSourceNode* MediaElementAudioSourceNode::create(
    BaseAudioContext& context,
    HTMLMediaElement& mediaElement,
    ExceptionState& exceptionState) {
  DCHECK(isMainThread());

  if (context.isContextClosed()) {
    context.throwExceptionForClosedState(exceptionState);
    return nullptr;
  }

  // First check if this media element already has a source node.
  if (mediaElement.audioSourceNode()) {
    exceptionState.throwDOMException(InvalidStateError,
                                     "HTMLMediaElement already connected "
                                     "previously to a different "
                                     "MediaElementSourceNode.");
    return nullptr;
  }

  MediaElementAudioSourceNode* node =
      new MediaElementAudioSourceNode(context, mediaElement);

  if (node) {
    mediaElement.setAudioSourceNode(node);
    // context keeps reference until node is disconnected
    context.notifySourceNodeStartedProcessing(node);
  }

  return node;
}

MediaElementAudioSourceNode* MediaElementAudioSourceNode::create(
    BaseAudioContext* context,
    const MediaElementAudioSourceOptions& options,
    ExceptionState& exceptionState) {
  if (!options.hasMediaElement()) {
    exceptionState.throwDOMException(NotFoundError,
                                     "mediaElement member is required.");
    return nullptr;
  }

  return create(*context, *options.mediaElement(), exceptionState);
}

DEFINE_TRACE(MediaElementAudioSourceNode) {
  AudioSourceProviderClient::trace(visitor);
  AudioSourceNode::trace(visitor);
}

MediaElementAudioSourceHandler&
MediaElementAudioSourceNode::mediaElementAudioSourceHandler() const {
  return static_cast<MediaElementAudioSourceHandler&>(handler());
}

HTMLMediaElement* MediaElementAudioSourceNode::mediaElement() const {
  return mediaElementAudioSourceHandler().mediaElement();
}

void MediaElementAudioSourceNode::setFormat(size_t numberOfChannels,
                                            float sampleRate) {
  mediaElementAudioSourceHandler().setFormat(numberOfChannels, sampleRate);
}

void MediaElementAudioSourceNode::lock() {
  mediaElementAudioSourceHandler().lock();
}

void MediaElementAudioSourceNode::unlock() {
  mediaElementAudioSourceHandler().unlock();
}

}  // namespace blink
