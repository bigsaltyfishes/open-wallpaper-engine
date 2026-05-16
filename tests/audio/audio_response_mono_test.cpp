#include "Audio/AudioResponseService.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace
{

std::atomic<bool> g_track_allocations { false };
std::atomic<std::size_t> g_largest_allocation { 0u };

void TrackAllocation(std::size_t size)
{
    if (!g_track_allocations.load(std::memory_order_relaxed)) {
        return;
    }

    std::size_t current = g_largest_allocation.load(std::memory_order_relaxed);
    while (current < size &&
           !g_largest_allocation.compare_exchange_weak(
               current,
               size,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

struct AllocationTrackingScope
{
    AllocationTrackingScope()
    {
        g_largest_allocation.store(0u, std::memory_order_relaxed);
        g_track_allocations.store(true, std::memory_order_relaxed);
    }

    ~AllocationTrackingScope()
    {
        g_track_allocations.store(false, std::memory_order_relaxed);
    }
};

} // namespace

void* operator new(std::size_t size)
{
    TrackAllocation(size);
    if (void* pointer = std::malloc(size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    TrackAllocation(size);
    if (void* pointer = std::malloc(size)) {
        return pointer;
    }
    throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace wallpaper::audio
{
namespace
{

constexpr uint32_t kSubmitSampleRate = 12'000u;
constexpr uint32_t kChunkFrameCount = 200u;
constexpr uint32_t kChunkSubmitCount = 6u;
constexpr uint32_t kRetainedFrameCapacity = kSubmitSampleRate * 2u;

AudioSpectrumSnapshot WaitForGeneration() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);

    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = CurrentAudioSpectrumSnapshot();
        if (snapshot.generation > 0u) {
            return snapshot;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return CurrentAudioSpectrumSnapshot();
}

bool HasNonZeroAverage64Bin(const AudioSpectrumSnapshot& snapshot) {
    for (const float bin : snapshot.average64) {
        if (bin != 0.0f) {
            return true;
        }
    }
    return false;
}

TEST(AudioResponseMonoTest, MonoSubmitUpdatesSnapshotAt12Khz) {
    ResetAudioResponseServiceForTesting();

    std::array<float, 200> samples {};
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = std::sin(static_cast<float>(index) * 0.05f);
    }

    std::string error;
    for (uint32_t submit = 0; submit < kChunkSubmitCount; ++submit) {
        ASSERT_TRUE(SubmitMonoAudioFrames(kSubmitSampleRate, static_cast<uint32_t>(samples.size()), samples.data(), &error))
            << error;
    }

    auto snapshot = WaitForGeneration();
    EXPECT_EQ(snapshot.sample_rate, 12'000u);
    EXPECT_EQ(snapshot.last_submit_sample_rate, kSubmitSampleRate);
    EXPECT_EQ(snapshot.accepted_frame_count, samples.size() * kChunkSubmitCount);
    EXPECT_GT(snapshot.generation, 0u);
    EXPECT_TRUE(HasNonZeroAverage64Bin(snapshot));
    EXPECT_EQ(snapshot.left64, snapshot.average64);
    EXPECT_EQ(snapshot.right64, snapshot.average64);
    EXPECT_EQ(snapshot.left32, snapshot.average32);
    EXPECT_EQ(snapshot.right32, snapshot.average32);
    EXPECT_EQ(snapshot.left16, snapshot.average16);
    EXPECT_EQ(snapshot.right16, snapshot.average16);
}

TEST(AudioResponseMonoTest, MonoSubmitRejectsInvalidInput) {
    ResetAudioResponseServiceForTesting();

    std::array<float, 200> samples {};
    std::string error;

    EXPECT_FALSE(SubmitMonoAudioFrames(0, kChunkFrameCount, samples.data(), &error));
    EXPECT_NE(error.find("sample_rate"), std::string::npos);

    error.clear();
    EXPECT_FALSE(SubmitMonoAudioFrames(kSubmitSampleRate, 0, samples.data(), &error));
    EXPECT_NE(error.find("frame_count"), std::string::npos);

    error.clear();
    EXPECT_FALSE(SubmitMonoAudioFrames(kSubmitSampleRate, kChunkFrameCount, nullptr, &error));
    EXPECT_NE(error.find("pcm_frames"), std::string::npos);
}

TEST(AudioResponseMonoTest, MonoSubmitRejectsNonAnalysisSampleRate) {
    ResetAudioResponseServiceForTesting();

    std::array<float, 200> samples {};
    std::string error;

    EXPECT_FALSE(SubmitMonoAudioFrames(48'000u, kChunkFrameCount, samples.data(), &error));
    EXPECT_NE(error.find("sample_rate"), std::string::npos);
    EXPECT_NE(error.find("12000"), std::string::npos);
}

TEST(AudioResponseMonoTest, OversizedMonoSubmitIsAcceptedAndAnalyzed) {
    ResetAudioResponseServiceForTesting();

    constexpr uint32_t oversized_frame_count = 24'200u;
    std::vector<float> samples(oversized_frame_count, 0.0f);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        samples[index] = std::sin(static_cast<float>(index) * 0.03f);
    }

    std::string error;
    ASSERT_TRUE(SubmitMonoAudioFrames(kSubmitSampleRate, oversized_frame_count, samples.data(), &error))
        << error;

    auto snapshot = WaitForGeneration();
    EXPECT_EQ(snapshot.sample_rate, 12'000u);
    EXPECT_EQ(snapshot.last_submit_sample_rate, kSubmitSampleRate);
    EXPECT_EQ(snapshot.accepted_frame_count, oversized_frame_count);
    EXPECT_GT(snapshot.generation, 0u);
    EXPECT_TRUE(HasNonZeroAverage64Bin(snapshot));
}

TEST(AudioResponseMonoTest, StereoCompatibilityWrapperDownmixesToMono) {
    ResetAudioResponseServiceForTesting();

    std::array<float, 400> stereo {};
    for (std::size_t frame = 0; frame < 200; ++frame) {
        stereo[frame * 2u] = 0.25f;
        stereo[(frame * 2u) + 1u] = 0.75f;
    }

    std::string error;
    for (uint32_t submit = 0; submit < kChunkSubmitCount; ++submit) {
        ASSERT_TRUE(SubmitAudioFrames(kSubmitSampleRate, kChunkFrameCount, stereo.data(), &error))
            << error;
    }

    auto stereo_snapshot = WaitForGeneration();

    ResetAudioResponseServiceForTesting();

    std::array<float, 200> mono {};
    mono.fill(0.5f);

    error.clear();
    for (uint32_t submit = 0; submit < kChunkSubmitCount; ++submit) {
        ASSERT_TRUE(SubmitMonoAudioFrames(kSubmitSampleRate, kChunkFrameCount, mono.data(), &error))
            << error;
    }

    auto mono_snapshot = WaitForGeneration();
    EXPECT_EQ(stereo_snapshot.sample_rate, 12'000u);
    EXPECT_EQ(stereo_snapshot.last_submit_sample_rate, kSubmitSampleRate);
    EXPECT_EQ(stereo_snapshot.accepted_frame_count, kChunkFrameCount * kChunkSubmitCount);
    EXPECT_GT(stereo_snapshot.generation, 0u);
    EXPECT_EQ(mono_snapshot.sample_rate, 12'000u);
    EXPECT_EQ(mono_snapshot.last_submit_sample_rate, kSubmitSampleRate);
    EXPECT_EQ(mono_snapshot.accepted_frame_count, kChunkFrameCount * kChunkSubmitCount);
    EXPECT_GT(mono_snapshot.generation, 0u);
    EXPECT_EQ(stereo_snapshot.average64, mono_snapshot.average64);
    EXPECT_EQ(stereo_snapshot.left64, stereo_snapshot.average64);
    EXPECT_EQ(stereo_snapshot.right64, stereo_snapshot.average64);
    EXPECT_EQ(mono_snapshot.left64, mono_snapshot.average64);
    EXPECT_EQ(mono_snapshot.right64, mono_snapshot.average64);
}

TEST(AudioResponseMonoTest, OversizedStereoSubmitDownmixesOnlyRetainedSamples) {
    ResetAudioResponseServiceForTesting();

    constexpr uint32_t oversized_frame_count = kRetainedFrameCapacity + 400u;
    std::vector<float> stereo(static_cast<std::size_t>(oversized_frame_count) * 2u, 0.0f);
    std::vector<float> retained_mono(kRetainedFrameCapacity, 0.0f);

    for (uint32_t frame = 0; frame < oversized_frame_count; ++frame) {
        const float left = frame < 400u ? 0.0f : std::sin(static_cast<float>(frame) * 0.03f);
        const float right = frame < 400u ? 0.0f : std::cos(static_cast<float>(frame) * 0.04f);
        const std::size_t stereo_index = static_cast<std::size_t>(frame) * 2u;
        stereo[stereo_index] = left;
        stereo[stereo_index + 1u] = right;

        if (frame >= oversized_frame_count - kRetainedFrameCapacity) {
            retained_mono[frame - (oversized_frame_count - kRetainedFrameCapacity)] = 0.5f * (left + right);
        }
    }

    std::string error;
    {
        AllocationTrackingScope allocation_tracking;
        ASSERT_TRUE(SubmitAudioFrames(kSubmitSampleRate, oversized_frame_count, stereo.data(), &error))
            << error;
    }

    auto stereo_snapshot = WaitForGeneration();
    EXPECT_EQ(stereo_snapshot.sample_rate, 12'000u);
    EXPECT_EQ(stereo_snapshot.last_submit_sample_rate, kSubmitSampleRate);
    EXPECT_EQ(stereo_snapshot.accepted_frame_count, oversized_frame_count);
    EXPECT_GT(stereo_snapshot.generation, 0u);
    EXPECT_TRUE(HasNonZeroAverage64Bin(stereo_snapshot));
    EXPECT_LT(g_largest_allocation.load(std::memory_order_relaxed), static_cast<std::size_t>(oversized_frame_count) * sizeof(float));

    ResetAudioResponseServiceForTesting();

    error.clear();
    ASSERT_TRUE(SubmitMonoAudioFrames(kSubmitSampleRate, kRetainedFrameCapacity, retained_mono.data(), &error))
        << error;

    auto mono_snapshot = WaitForGeneration();
    EXPECT_GT(mono_snapshot.generation, 0u);
    EXPECT_EQ(stereo_snapshot.average64, mono_snapshot.average64);
}

TEST(AudioResponseMonoTest, StereoCompatibilityWrapperRejectsNonAnalysisSampleRate) {
    ResetAudioResponseServiceForTesting();

    std::array<float, 400> stereo {};
    std::string error;

    EXPECT_FALSE(SubmitAudioFrames(48'000u, kChunkFrameCount, stereo.data(), &error));
    EXPECT_NE(error.find("sample_rate"), std::string::npos);
    EXPECT_NE(error.find("12000"), std::string::npos);
}

} // namespace
} // namespace wallpaper::audio
