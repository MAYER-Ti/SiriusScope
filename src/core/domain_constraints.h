#pragma once

/*!
 * \file domain_constraints.h
 * \brief Доменные ограничения SiriusScope и флаги возможностей времени выполнения.
 */

#include <cstdint>

namespace siriusscope::core {

/*!
 * \brief Ограничения времени выполнения, зависящие от конфигурации изделия.
 *
 * Текущая итерация использует пять полос и два активных луча. Максимальное
 * поддерживаемое число лучей хранится явно, чтобы будущая поддержка 8 лучей
 * не была заблокирована жесткими предположениями UI.
 */
struct RuntimeCapabilities
{
    //! Количество настроенных полос BCO, доступных обработке и UI-моделям.
    int bandCount = 5;
    //! Количество индексов лучей, принимаемых во входных отсчетах.
    int activeBeamCount = 2;
    //! Верхняя граница, поддерживаемая текущей доменной моделью и адаптерами.
    int maxSupportedBeamCount = 8;
};

/*!
 * \brief Ограничения изделия и текущей итерации, используемые валидацией.
 */
namespace DomainConstraints {

//! Минимальная допустимая входная амплитуда. Ноль является ошибочным входом.
inline constexpr int minAmplitude = 1;
//! Максимальная допустимая входная амплитуда из потока BCO.
inline constexpr int maxAmplitude = 127;

//! Количество объектов BandItem в текущей итерации SiriusScope.
inline constexpr int currentBandCount = 5;
//! Количество активных лучей в текущей модели антенны.
inline constexpr int currentBeamCount = 2;
//! Зарезервированная верхняя граница для будущей поддержки 8-лучевой антенны.
inline constexpr int futureMaxBeamCount = 8;

//! Нижняя граница полного частотного диапазона изделия, Гц.
inline constexpr std::int64_t minSystemFrequencyHz = 300'000'000LL;
//! Верхняя граница полного частотного диапазона изделия, Гц.
inline constexpr std::int64_t maxSystemFrequencyHz = 18'000'000'000LL;
//! Максимальная ширина одной полосы BCO, представленной BandItem, Гц.
inline constexpr std::int64_t maxBandWidthHz = 500'000'000LL;
//! Максимальное знаковое смещение от центра полосы полной ширины.
inline constexpr std::int64_t maxFrequencyOffsetHz = maxBandWidthHz / 2;

//! Период отсчета по умолчанию, если протокольные метаданные не задали другой.
inline constexpr std::uint64_t defaultSamplePeriodNs = 320ULL;

//! Включительная нижняя граница азимута, градусы.
inline constexpr double minAzimuthDeg = 0.0;
//! Исключительная верхняя граница азимута, градусы.
inline constexpr double maxAzimuthDeg = 360.0;
//! Минимальное допустимое нормализованное значение качества.
inline constexpr double minQuality = 0.0;
//! Максимальное допустимое нормализованное значение качества.
inline constexpr double maxQuality = 1.0;

} // namespace DomainConstraints

/*!
 * \brief Возвращает возможности, соответствующие текущей итерации изделия.
 *
 * \return Пять полос, два активных луча и резерв под поддержку до восьми лучей.
 */
RuntimeCapabilities defaultRuntimeCapabilities() noexcept;

} // namespace siriusscope::core
