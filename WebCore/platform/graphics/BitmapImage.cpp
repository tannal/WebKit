/*
 * Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2004, 2005, 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "BitmapImage.h"

#include "FloatRect.h"
#include "ImageObserver.h"
#include "IntRect.h"
#include "PlatformString.h"
#include "Timer.h"
#include <wtf/Vector.h>
#include "MimeTypeRegistry.h"

namespace WebCore {

BitmapImage::BitmapImage(ImageObserver* observer)
    : Image(observer)
    , m_currentFrame(0)
    , m_frames(0)
    , m_frameTimer(0)
    , m_repetitionCount(0)
    , m_repetitionsComplete(0)
    , m_isSolidColor(false)
    , m_animatingImageType(true)
    , m_animationFinished(false)
    , m_haveSize(false)
    , m_sizeAvailable(false)
    , m_decodedSize(0)
{
    initPlatformData();
}

BitmapImage::~BitmapImage()
{
    // Null out the image observer so that we don't incorrectly communicate that decoded data is being destroyed during destruction.
    m_imageObserver = 0;
    destroyDecodedData();
    stopAnimation();
}

void BitmapImage::destroyDecodedData(bool incremental)
{
    // Destroy the cached images and release them.
    if (m_frames.size()) {
        int sizeChange = 0;
        int frameSize = m_size.width() * m_size.height() * 4;
        for (unsigned i = incremental ? m_frames.size() - 1 : 0; i < m_frames.size(); i++) {
            if (m_frames[i].m_frame) {
                sizeChange -= frameSize;
                m_frames[i].clear();
                m_source.destroyFrameAtIndex(i);
            }
        }

        // We just always invalidate our platform data, even in the incremental case.
        // This could be better, but it's not a big deal.
        m_isSolidColor = false;
        invalidatePlatformData();
        
        if (sizeChange) {
            m_decodedSize += sizeChange;
            if (imageObserver())
                imageObserver()->decodedSizeChanged(this, sizeChange);
        }
    }
}

void BitmapImage::cacheFrame(size_t index)
{
    size_t numFrames = frameCount();
    if (!m_frames.size() && shouldAnimate()) {            
        // Snag the repetition count.
        m_repetitionCount = m_source.repetitionCount();
        if (m_repetitionCount == cAnimationNone)
            m_animatingImageType = false;
    }
    
    if (m_frames.size() < numFrames)
        m_frames.resize(numFrames);

    m_frames[index].m_frame = m_source.createFrameAtIndex(index);
    if (numFrames == 1 && m_frames[index].m_frame)
        checkForSolidColor();

    if (shouldAnimate())
        m_frames[index].m_duration = m_source.frameDurationAtIndex(index);
    m_frames[index].m_hasAlpha = m_source.frameHasAlphaAtIndex(index);
    
    int sizeChange = m_size.width() * m_size.height() * 4;
    if (sizeChange) {
        m_decodedSize += sizeChange;
        if (imageObserver())
            imageObserver()->decodedSizeChanged(this, sizeChange);
    }
}

IntSize BitmapImage::size() const
{
    if (m_sizeAvailable && !m_haveSize) {
        m_size = m_source.size();
        m_haveSize = true;
    }
    return m_size;
}

bool BitmapImage::setNativeData(NativeBytePtr data, bool allDataReceived)
{
    destroyDecodedData(true);
    
    // Feed all the data we've seen so far to the image decoder.
    m_source.setData(data, allDataReceived);
    
    // Image properties will not be available until the first frame of the file
    // reaches kCGImageStatusIncomplete.
    return isSizeAvailable();
}

size_t BitmapImage::frameCount()
{
    return m_source.frameCount();
}

bool BitmapImage::isSizeAvailable()
{
    if (m_sizeAvailable)
        return true;

    m_sizeAvailable = m_source.isSizeAvailable();

    return m_sizeAvailable;
}

NativeImagePtr BitmapImage::frameAtIndex(size_t index)
{
    if (index >= frameCount())
        return 0;

    if (index >= m_frames.size() || !m_frames[index].m_frame)
        cacheFrame(index);

    return m_frames[index].m_frame;
}

float BitmapImage::frameDurationAtIndex(size_t index)
{
    if (index >= frameCount())
        return 0;

    if (index >= m_frames.size() || !m_frames[index].m_frame)
        cacheFrame(index);

    return m_frames[index].m_duration;
}

bool BitmapImage::frameHasAlphaAtIndex(size_t index)
{
    if (index >= frameCount())
        return 0;

    if (index >= m_frames.size() || !m_frames[index].m_frame)
        cacheFrame(index);

    return m_frames[index].m_hasAlpha;
}

bool BitmapImage::shouldAnimate()
{
    return (m_animatingImageType && !m_animationFinished && imageObserver());
}

void BitmapImage::startAnimation()
{
    if (m_frameTimer || !shouldAnimate() || frameCount() <= 1)
        return;

    m_frameTimer = new Timer<BitmapImage>(this, &BitmapImage::advanceAnimation);
    m_frameTimer->startOneShot(frameDurationAtIndex(m_currentFrame));
}

void BitmapImage::stopAnimation()
{
    // This timer is used to animate all occurrences of this image.  Don't invalidate
    // the timer unless all renderers have stopped drawing.
    delete m_frameTimer;
    m_frameTimer = 0;
}

void BitmapImage::resetAnimation()
{
    stopAnimation();
    m_currentFrame = 0;
    m_repetitionsComplete = 0;
    m_animationFinished = false;
}

void BitmapImage::advanceAnimation(Timer<BitmapImage>* timer)
{
    // Stop the animation.
    stopAnimation();
    
    // See if anyone is still paying attention to this animation.  If not, we don't
    // advance and will remain suspended at the current frame until the animation is resumed.
    if (imageObserver()->shouldPauseAnimation(this))
        return;

    m_currentFrame++;
    if (m_currentFrame >= frameCount()) {
        m_repetitionsComplete += 1;
        if (m_repetitionCount && m_repetitionsComplete >= m_repetitionCount) {
            m_animationFinished = true;
            m_currentFrame--;
            return;
        }
        m_currentFrame = 0;
    }

    // Notify our observer that the animation has advanced.
    imageObserver()->animationAdvanced(this);
        
    // Kick off a timer to move to the next frame.
    m_frameTimer = new Timer<BitmapImage>(this, &BitmapImage::advanceAnimation);
    m_frameTimer->startOneShot(frameDurationAtIndex(m_currentFrame));
}

}
