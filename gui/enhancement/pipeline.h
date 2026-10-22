// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#pragma once
#include "protocol.h"
#include <QObject>
#include <memory>
extern "C"
{
#include <libplacebo/vulkan.h>
#include <libplacebo/renderer.h>
}

struct EnhancementOptions
{
    bool neural = false, fsr = true, frameGeneration = false, bridgeTest = false;
    int percent = 65, sharpness = 20;
    bool operator==(const EnhancementOptions &o) const
    {
        return neural == o.neural && fsr == o.fsr && frameGeneration == o.frameGeneration &&
               bridgeTest == o.bridgeTest && percent == o.percent && sharpness == o.sharpness;
    }
};

// All GPU/process methods run on the libplacebo render thread. Only copied
// status values and frame notifications cross to the Qt GUI thread.
class EnhancementPipeline : public QObject
{
    Q_OBJECT
  public:
    explicit EnhancementPipeline(QObject *parent = nullptr);
    ~EnhancementPipeline() override;
    void prepare(pl_vulkan vk, chiaki_enhancement::Extent native, const EnhancementOptions &options,
                 bool hdr);
    pl_tex beginFrame(uint64_t frameId, uint64_t timestampUs);
    bool submitFrame();
    bool outputFrame(pl_frame &frame);
    uint64_t completedFrameId() const;
    bool frameGenerationReady() const;
    bool ownsPresentation() const;
    bool hasUnpresentedOutput() const;
    void setSourceInterval(uint64_t intervalUs);
    bool generatedOutput() const;
    void notePresented(uint64_t presentationWorkUs = 0);
    uint64_t realPresentedCount() const;
    uint64_t generatedPresentedCount() const;
    void reset();
  signals:
    void applyingChanged(bool applying);
    void statusChanged(QString message, int nativeWidth, int nativeHeight, int nrWidth,
                       int nrHeight, bool nrActive, bool fsrActive, bool fgActive);
    void frameAvailable();
    void neuralUnavailable(QString reason);
    void cadenceChanged(QString summary);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
