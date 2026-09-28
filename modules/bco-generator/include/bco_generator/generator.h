#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace bco_generator {

//! Независимая запись генератора; не является сетевым форматом БЦО.
struct Sample
{
    std::uint64_t sampleIndex = 0;
    int bandIndex = 0;
    std::int64_t frequencyOffsetHz = 0;
    std::int64_t absoluteFrequencyHz = 0;
    int amplitude = 0;
    int beamIndex = 0;
};

//! Полоса приема. Массив запроса содержит только включенные полосы.
struct Band
{
    int bandIndex = 0;
    std::int64_t centerFrequencyHz = 0;
    std::int64_t minFrequencyHz = 0;
    std::int64_t maxFrequencyHz = 0;
    // Результат проверки конфигурации вызывающей стороной; старый fast path
    // не использует этот флаг, физическая модель пропускает невалидные полосы.
    bool valid = true;
    bool containsFrequency(std::int64_t hz) const noexcept
    { return hz >= minFrequencyHz && hz <= maxFrequencyHz; }
};

struct PulseConfig
{
    int bandIndex = 0;
    bool enabled = true;
    double pulsePeriodUs = 100000.0;
    double pulseWidthUs = 10000.0;
};

struct RadioSource
{
    double azimuthDeg = 45.0;
    std::int64_t absoluteFrequencyHz = 0;
    int peakAmplitude = 110;
    double beamSigmaDeg = 22.0;
    bool frequencyDriftEnabled = false;
    std::int64_t driftSpanHz = 5'000'000;
    std::uint64_t driftPeriodSteps = 60;
};

//! Данные заимствуются только на время generate(); состояние принадлежит runner.
struct BatchRequest
{
    std::span<const Band> bands;
    std::span<const PulseConfig> pulses;
    std::span<const RadioSource> sources;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t timeBaseFirstSampleIndex = 0;
    std::uint64_t samplePeriodNs = 1;
    double antennaAzimuthDeg = 0.0;
    int minVisibleAmplitude = 0;
    bool fastContinuous = false;
};

enum class Status { Ok, InsufficientScratch };
struct BatchResult
{
    Status status = Status::Ok;
    std::size_t sampleCount = 0;
    std::uint64_t nextSampleIndex = 0;
};

//! Проверка PRI/PW без логирования и платформенных зависимостей.
bool hasValidPulseTiming(const PulseConfig& config) noexcept;

//! Заполняет output, без heap, часов, потоков и callbacks.
//! output.size() задает бюджет записей И временных слотов старой модели.
//! Для fastContinuous без pulses нужен scratch размером не менее 2*sources.size().
//! При нехватке scratch output не изменяется и время не продвигается.
//! Входные spans, output и scratch не должны перекрываться; lifetime — до возврата.
//! Нечетный бюджет обрезает пару лучей (зафиксированная совместимость).
BatchResult generate(const BatchRequest& request, std::span<Sample> output,
                     std::span<Sample> scratch = {}) noexcept;

} // namespace bco_generator
