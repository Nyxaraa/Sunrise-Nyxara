#pragma once

#include <cstddef>
#include <cstdint>

#include "../../patterns/image_scan.h"

namespace sunrise::client::hooks::teleport {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

/**
 * Object-handle index bits. Two handles name the same object exactly when these bits match, so
 * comparing them is the whole ownership test. No datum array lookup is needed.
 */
inline constexpr std::uint32_t kHandleIndexMask = 0x1FFF;
/** The controlled-object getter writes this when the local player owns no object. */
inline constexpr std::uint32_t kInvalidHandle = 0xFFFFFFFF;

/** Camera pose block stride, indexed by player. Its vectors are plain floats. */
inline constexpr std::size_t kCameraBlockStride = 0xC50;
/** Camera pose fields inside one player camera block. */
inline constexpr std::size_t kCameraPositionX = 0x594;
/** Camera forward vector. Its default is (1,0,0), so the basis is X forward, Z up. */
inline constexpr std::size_t kCameraForwardX = kCameraPositionX + 0x28;
inline constexpr std::size_t kCameraUpX = kCameraPositionX + 0x34;
inline constexpr std::size_t kCameraHorizontalFov = kCameraPositionX + 0x40;
inline constexpr std::size_t kCameraAspect = kCameraPositionX + 0xB8;

/** Object handle the physics component drives, as a u16. */
inline constexpr std::size_t kPhysicsComponentObjectHandle = 44;

/** Rigid-body array on the physics component. */
inline constexpr std::size_t kPhysicsComponentBodyArray = 400;
/** Index into that array, signed. */
inline constexpr std::size_t kPhysicsComponentBodyIndex = 516;
/** Array entry stride, and the body pointer inside one entry. */
inline constexpr std::size_t kBodyEntryStride = 80;
inline constexpr std::size_t kBodyPointer = 32;

/**
 * Swept-transform centre of mass 1 of the body's motion state. The transform translation at
 * +416 is rebuilt from it by the physics step, so a body at rest does not follow a write here.
 */
inline constexpr std::size_t kBodyPositionX = 448;
/** Rigid-body velocity. The sync copies this into the physics component every tick. */
inline constexpr std::size_t kBodyVelocityX = 560;

} // namespace sunrise::client::hooks::teleport
