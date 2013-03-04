/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GRAPHIC_BUFFER_SOURCE_H_

#define GRAPHIC_BUFFER_SOURCE_H_

#include <gui/IGraphicBufferProducer.h>
#include <gui/BufferQueue.h>
#include <utils/RefBase.h>

#include <OMX_Core.h>
#include "../include/OMXNodeInstance.h"
#include <media/stagefright/foundation/ABase.h>

namespace android {

/*
 * This class is used to feed OMX codecs from a Surface via BufferQueue.
 *
 * Instances of the class don't run on a dedicated thread.  Instead,
 * various events trigger data movement:
 *
 *  - Availability of a new frame of data from the BufferQueue (notified
 *    via the onFrameAvailable callback).
 *  - The return of a codec buffer (via OnEmptyBufferDone).
 *  - Application signaling end-of-stream.
 *  - Transition to or from "executing" state.
 *
 * Frames of data (and, perhaps, the end-of-stream indication) can arrive
 * before the codec is in the "executing" state, so we need to queue
 * things up until we're ready to go.
 */
class GraphicBufferSource : public BufferQueue::ConsumerListener {
public:
    GraphicBufferSource(OMXNodeInstance* nodeInstance,
            uint32_t bufferWidth, uint32_t bufferHeight);
    virtual ~GraphicBufferSource();

    // We can't throw an exception if the constructor fails, so we just set
    // this and require that the caller test the value.
    status_t initCheck() const {
        return mInitCheck;
    }

    // Returns the handle to the producer side of the BufferQueue.  Buffers
    // queued on this will be received by GraphicBufferSource.
    sp<IGraphicBufferProducer> getIGraphicBufferProducer() const {
        return mBufferQueue;
    }

    // This is called when OMX transitions to OMX_StateExecuting, which means
    // we can start handing it buffers.  If we already have buffers of data
    // sitting in the BufferQueue, this will send them to the codec.
    void omxExecuting();

    // This is called when OMX transitions to OMX_StateLoaded, indicating that
    // we are shutting down.
    void omxLoaded();

    // A "codec buffer", i.e. a buffer that can be used to pass data into
    // the encoder, has been allocated.  (This call does not call back into
    // OMXNodeInstance.)
    void addCodecBuffer(OMX_BUFFERHEADERTYPE* header);

    // Called from OnEmptyBufferDone.  If we have a BQ buffer available,
    // fill it with a new frame of data; otherwise, just mark it as available.
    void codecBufferEmptied(OMX_BUFFERHEADERTYPE* header);

    // This is called after the last input frame has been submitted.  We
    // need to submit an empty buffer with the EOS flag set.  If we don't
    // have a codec buffer ready, we just set the mEndOfStream flag.
    status_t signalEndOfInputStream();

protected:
    // BufferQueue::ConsumerListener interface, called when a new frame of
    // data is available.  If we're executing and a codec buffer is
    // available, we acquire the buffer, copy the GraphicBuffer reference
    // into the codec buffer, and call Empty[This]Buffer.  If we're not yet
    // executing or there's no codec buffer available, we just increment
    // mNumFramesAvailable and return.
    virtual void onFrameAvailable();

    // BufferQueue::ConsumerListener interface, called when the client has
    // released one or more GraphicBuffers.  We clear out the appropriate
    // set of mBufferSlot entries.
    virtual void onBuffersReleased();

private:
    // Keep track of codec input buffers.  They may either be available
    // (mGraphicBuffer == NULL) or in use by the codec.
    struct CodecBuffer {
        OMX_BUFFERHEADERTYPE* mHeader;
        sp<GraphicBuffer> mGraphicBuffer;
    };

    // Returns the index of an available codec buffer.  If none are
    // available, returns -1.  Mutex must be held by caller.
    int findAvailableCodecBuffer_l();

    // Returns true if a codec buffer is available.
    bool isCodecBufferAvailable_l() {
        return findAvailableCodecBuffer_l() >= 0;
    }

    // Finds the mCodecBuffers entry that matches.  Returns -1 if not found.
    int findMatchingCodecBuffer_l(const OMX_BUFFERHEADERTYPE* header);

    // Fills a codec buffer with a frame from the BufferQueue.  This must
    // only be called when we know that a frame of data is ready (i.e. we're
    // in the onFrameAvailable callback, or if we're in codecBufferEmptied
    // and mNumFramesAvailable is nonzero).  Returns without doing anything if
    // we don't have a codec buffer available.
    status_t fillCodecBuffer_l();

    // Marks the mCodecBuffers entry as in-use, copies the GraphicBuffer
    // reference into the codec buffer, and submits the data to the codec.
    status_t submitBuffer_l(sp<GraphicBuffer>& graphicBuffer,
            int64_t timestamp, int cbi);

    // Submits an empty buffer, with the EOS flag set.   Returns without
    // doing anything if we don't have a codec buffer available.
    void submitEndOfInputStream_l();

    // Lock, covers all member variables.
    mutable Mutex mMutex;

    // Used to report constructor failure.
    status_t mInitCheck;

    // Pointer back to the object that contains us.  We send buffers here.
    OMXNodeInstance* mNodeInstance;

    // Set by omxExecuting() / omxIdling().
    bool mExecuting;

    // We consume graphic buffers from this.
    sp<BufferQueue> mBufferQueue;

    // Number of frames pending in BufferQueue that haven't yet been
    // forwarded to the codec.
    size_t mNumFramesAvailable;

    // Set to true if we want to send end-of-stream after we run out of
    // frames in BufferQueue.
    bool mEndOfStream;
    bool mEndOfStreamSent;

    // Cache of GraphicBuffers from the buffer queue.  When the codec
    // is done processing a GraphicBuffer, we can use this to map back
    // to a slot number.
    sp<GraphicBuffer> mBufferSlot[BufferQueue::NUM_BUFFER_SLOTS];

    // Tracks codec buffers.
    Vector<CodecBuffer> mCodecBuffers;

    DISALLOW_EVIL_CONSTRUCTORS(GraphicBufferSource);
};

}  // namespace android

#endif  // GRAPHIC_BUFFER_SOURCE_H_