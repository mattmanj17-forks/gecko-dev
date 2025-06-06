/*
 *  Copyright 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "pc/codec_vendor.h"

#include <stddef.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/field_trials_view.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "call/payload_type.h"
#include "media/base/codec.h"
#include "media/base/codec_comparators.h"
#include "media/base/codec_list.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/sdp_video_format_utils.h"
#include "pc/media_options.h"
#include "pc/rtp_media_utils.h"
#include "pc/session_description.h"
#include "pc/typed_codec_vendor.h"
#include "pc/used_ids.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/unique_id_generator.h"

#ifdef RTC_ENABLE_H265
#include "api/video_codecs/h265_profile_tier_level.h"
#endif

namespace cricket {

namespace {

using rtc::UniqueRandomIdGenerator;
using webrtc::PayloadTypeSuggester;
using webrtc::RTCError;
using webrtc::RTCErrorOr;
using webrtc::RTCErrorType;
using webrtc::RtpTransceiverDirection;

bool IsRtxCodec(const webrtc::RtpCodecCapability& capability) {
  return absl::EqualsIgnoreCase(capability.name, kRtxCodecName);
}

bool IsRedCodec(const webrtc::RtpCodecCapability& capability) {
  return absl::EqualsIgnoreCase(capability.name, kRedCodecName);
}

bool IsComfortNoiseCodec(const Codec& codec) {
  return absl::EqualsIgnoreCase(codec.name, kComfortNoiseCodecName);
}

// Wrapper for FindMatchingCodecs that uses CodecList
std::optional<Codec> FindMatchingCodec(const CodecList& codecs1,
                                       const CodecList& codecs2,
                                       const Codec& codec_to_match) {
  return webrtc::FindMatchingCodec(codecs1.codecs(), codecs2.codecs(),
                                   codec_to_match);
}

void StripCNCodecs(CodecList& audio_codecs) {
  audio_codecs.writable_codecs().erase(
      std::remove_if(
          audio_codecs.begin(), audio_codecs.end(),
          [](const Codec& codec) { return IsComfortNoiseCodec(codec); }),
      audio_codecs.end());
}

bool IsMediaContentOfType(const webrtc::ContentInfo* content,
                          webrtc::MediaType media_type) {
  if (!content || !content->media_description()) {
    return false;
  }
  return content->media_description()->type() == media_type;
}
// Find the codec in `codec_list` that `rtx_codec` is associated with.
const Codec* GetAssociatedCodecForRtx(const CodecList& codec_list,
                                      const Codec& rtx_codec) {
  std::string associated_pt_str;
  if (!rtx_codec.GetParam(kCodecParamAssociatedPayloadType,
                          &associated_pt_str)) {
    RTC_LOG(LS_WARNING) << "RTX codec " << rtx_codec.id
                        << " is missing an associated payload type.";
    return nullptr;
  }

  int associated_pt;
  if (!rtc::FromString(associated_pt_str, &associated_pt)) {
    RTC_LOG(LS_WARNING) << "Couldn't convert payload type " << associated_pt_str
                        << " of RTX codec " << rtx_codec.id
                        << " to an integer.";
    return nullptr;
  }

  // Find the associated codec for the RTX codec.
  const Codec* associated_codec =
      FindCodecById(codec_list.codecs(), associated_pt);
  if (!associated_codec) {
    RTC_LOG(LS_WARNING) << "Couldn't find associated codec with payload type "
                        << associated_pt << " for RTX codec " << rtx_codec.id
                        << ".";
  }
  return associated_codec;
}

// Find the codec in `codec_list` that `red_codec` is associated with.
const Codec* GetAssociatedCodecForRed(const CodecList& codec_list,
                                      const Codec& red_codec) {
  std::string fmtp;
  if (!red_codec.GetParam(kCodecParamNotInNameValueFormat, &fmtp)) {
    // Don't log for video/RED where this is normal.
    if (red_codec.type == Codec::Type::kAudio) {
      RTC_LOG(LS_WARNING) << "RED codec " << red_codec.id
                          << " is missing an associated payload type.";
    }
    return nullptr;
  }

  std::vector<absl::string_view> redundant_payloads = rtc::split(fmtp, '/');
  if (redundant_payloads.size() < 2) {
    return nullptr;
  }

  absl::string_view associated_pt_str = redundant_payloads[0];
  int associated_pt;
  if (!rtc::FromString(associated_pt_str, &associated_pt)) {
    RTC_LOG(LS_WARNING) << "Couldn't convert first payload type "
                        << associated_pt_str << " of RED codec " << red_codec.id
                        << " to an integer.";
    return nullptr;
  }

  // Find the associated codec for the RED codec.
  const Codec* associated_codec =
      FindCodecById(codec_list.codecs(), associated_pt);
  if (!associated_codec) {
    RTC_LOG(LS_WARNING) << "Couldn't find associated codec with payload type "
                        << associated_pt << " for RED codec " << red_codec.id
                        << ".";
  }
  return associated_codec;
}

// Adds all codecs from `reference_codecs` to `offered_codecs` that don't
// already exist in `offered_codecs` and ensure the payload types don't
// collide.
void MergeCodecs(const CodecList& reference_codecs,
                 CodecList& offered_codecs,
                 UsedPayloadTypes* used_pltypes) {
  // Add all new codecs that are not RTX/RED codecs.
  // The two-pass splitting of the loops means preferring payload types
  // of actual codecs with respect to collisions.
  for (const Codec& reference_codec : reference_codecs) {
    if (reference_codec.GetResiliencyType() != Codec::ResiliencyType::kRtx &&
        reference_codec.GetResiliencyType() != Codec::ResiliencyType::kRed &&
        !FindMatchingCodec(reference_codecs, offered_codecs, reference_codec)) {
      Codec codec = reference_codec;
      used_pltypes->FindAndSetIdUsed(&codec);
      offered_codecs.push_back(codec);
    }
  }

  // Add all new RTX or RED codecs.
  for (const Codec& reference_codec : reference_codecs) {
    if (reference_codec.GetResiliencyType() == Codec::ResiliencyType::kRtx &&
        !FindMatchingCodec(reference_codecs, offered_codecs, reference_codec)) {
      Codec rtx_codec = reference_codec;
      const Codec* associated_codec =
          GetAssociatedCodecForRtx(reference_codecs, rtx_codec);
      if (!associated_codec) {
        continue;
      }
      // Find a codec in the offered list that matches the reference codec.
      // Its payload type may be different than the reference codec.
      std::optional<Codec> matching_codec = FindMatchingCodec(
          reference_codecs, offered_codecs, *associated_codec);
      if (!matching_codec) {
        RTC_LOG(LS_WARNING)
            << "Couldn't find matching " << associated_codec->name << " codec.";
        continue;
      }

      rtx_codec.params[kCodecParamAssociatedPayloadType] =
          rtc::ToString(matching_codec->id);
      used_pltypes->FindAndSetIdUsed(&rtx_codec);
      offered_codecs.push_back(rtx_codec);
    } else if (reference_codec.GetResiliencyType() ==
                   Codec::ResiliencyType::kRed &&
               !FindMatchingCodec(reference_codecs, offered_codecs,
                                  reference_codec)) {
      Codec red_codec = reference_codec;
      const Codec* associated_codec =
          GetAssociatedCodecForRed(reference_codecs, red_codec);
      if (associated_codec) {
        std::optional<Codec> matching_codec = FindMatchingCodec(
            reference_codecs, offered_codecs, *associated_codec);
        if (!matching_codec) {
          RTC_LOG(LS_WARNING) << "Couldn't find matching "
                              << associated_codec->name << " codec.";
          continue;
        }

        red_codec.params[kCodecParamNotInNameValueFormat] =
            rtc::ToString(matching_codec->id) + "/" +
            rtc::ToString(matching_codec->id);
      }
      used_pltypes->FindAndSetIdUsed(&red_codec);
      offered_codecs.push_back(red_codec);
    }
  }
  offered_codecs.CheckConsistency();
}

// `codecs` is a full list of codecs with correct payload type mappings, which
// don't conflict with mappings of the other media type; `supported_codecs` is
// a list filtered for the media section`s direction but with default payload
// types.
// static
CodecList MatchCodecPreference(
    const std::vector<webrtc::RtpCodecCapability>& codec_preferences,
    const CodecList& codecs,
    const CodecList& supported_codecs) {
  CodecList filtered_codecs;
  bool want_rtx = false;
  bool want_red = false;

  for (const auto& codec_preference : codec_preferences) {
    if (IsRtxCodec(codec_preference)) {
      want_rtx = true;
    } else if (IsRedCodec(codec_preference)) {
      want_red = true;
    }
  }
  bool red_was_added = false;
  for (const auto& codec_preference : codec_preferences) {
    auto found_codec = absl::c_find_if(
        supported_codecs, [&codec_preference](const Codec& codec) {
          // We should not filter out the codec in |codec_preferences| if it
          // has a higher level than the codec in |supported_codecs|, as the
          // codec in |supported_codecs| may be only with lower level in
          // |send_codecs_| and |recv_codecs_| for the same codec.
          return IsSameRtpCodecIgnoringLevel(codec, codec_preference);
        });

    if (found_codec != supported_codecs.end()) {
      std::optional<Codec> found_codec_with_correct_pt =
          FindMatchingCodec(supported_codecs, codecs, *found_codec);
      if (found_codec_with_correct_pt) {
        // RED may already have been added if its primary codec is before RED
        // in the codec list.
        bool is_red_codec = found_codec_with_correct_pt->GetResiliencyType() ==
                            Codec::ResiliencyType::kRed;
        if (!is_red_codec || !red_was_added) {
          filtered_codecs.push_back(*found_codec_with_correct_pt);
          red_was_added = is_red_codec ? true : red_was_added;
        }
        std::string id = rtc::ToString(found_codec_with_correct_pt->id);
        // Search for the matching rtx or red codec.
        if (want_red || want_rtx) {
          for (const auto& codec : codecs) {
            if (want_rtx &&
                codec.GetResiliencyType() == Codec::ResiliencyType::kRtx) {
              const auto apt =
                  codec.params.find(cricket::kCodecParamAssociatedPayloadType);
              if (apt != codec.params.end() && apt->second == id) {
                filtered_codecs.push_back(codec);
                break;
              }
            } else if (want_red && codec.GetResiliencyType() ==
                                       Codec::ResiliencyType::kRed) {
              // For RED, do not insert the codec again if it was already
              // inserted. audio/red for opus gets enabled by having RED before
              // the primary codec.
              const auto fmtp =
                  codec.params.find(cricket::kCodecParamNotInNameValueFormat);
              if (fmtp != codec.params.end()) {
                std::vector<absl::string_view> redundant_payloads =
                    rtc::split(fmtp->second, '/');
                if (!redundant_payloads.empty() &&
                    redundant_payloads[0] == id) {
                  if (!red_was_added) {
                    filtered_codecs.push_back(codec);
                    red_was_added = true;
                  }
                  break;
                }
              }
            }
          }
        }
      }
    }
  }

  return filtered_codecs;
}

// Compute the union of `codecs1` and `codecs2`.
CodecList ComputeCodecsUnion(const CodecList codecs1, const CodecList codecs2) {
  CodecList all_codecs;
  UsedPayloadTypes used_payload_types;
  for (const Codec& codec : codecs1) {
    Codec codec_mutable = codec;
    used_payload_types.FindAndSetIdUsed(&codec_mutable);
    all_codecs.push_back(codec_mutable);
  }

  // Use MergeCodecs to merge the second half of our list as it already checks
  // and fixes problems with duplicate payload types.
  MergeCodecs(codecs2, all_codecs, &used_payload_types);

  return all_codecs;
}

RTCError MergeCodecsFromDescription(
    const std::vector<const webrtc::ContentInfo*>& current_active_contents,
    CodecList& audio_codecs,
    CodecList& video_codecs,
    UsedPayloadTypes* used_pltypes) {
  for (const ContentInfo* content : current_active_contents) {
    RTCErrorOr<CodecList> checked_codec_list =
        CodecList::Create(content->media_description()->codecs());
    if (!checked_codec_list.ok()) {
      RTC_LOG(LS_ERROR) << checked_codec_list.error();
    }
    if (IsMediaContentOfType(content, webrtc::MediaType::AUDIO)) {
      MergeCodecs(checked_codec_list.value(), audio_codecs, used_pltypes);
    } else if (IsMediaContentOfType(content, webrtc::MediaType::VIDEO)) {
      MergeCodecs(checked_codec_list.value(), video_codecs, used_pltypes);
    }
  }
  return RTCError::OK();
}

void NegotiatePacketization(const Codec& local_codec,
                            const Codec& remote_codec,
                            Codec* negotiated_codec) {
  negotiated_codec->packetization =
      (local_codec.packetization == remote_codec.packetization)
          ? local_codec.packetization
          : std::nullopt;
}

#ifdef RTC_ENABLE_H265
void NegotiateTxMode(const Codec& local_codec,
                     const Codec& remote_codec,
                     Codec* negotiated_codec) {
  negotiated_codec->tx_mode = (local_codec.tx_mode == remote_codec.tx_mode)
                                  ? local_codec.tx_mode
                                  : std::nullopt;
}
#endif

// For offer, negotiated codec must have the same level-id as that in
// |supported_codecs| with same profile.
void NegotiateVideoCodecLevelsForOffer(
    const MediaDescriptionOptions& media_description_options,
    const CodecList& supported_codecs,
    CodecList& filtered_codecs) {
  if (filtered_codecs.empty() || supported_codecs.empty()) {
    return;
  }

  // TODO(http://crbugs.com/376306259): We should handle level-idx for AV1.
  // Ideally this should be done for all codecs, but RFCs of other codecs
  // do not clear define the expected behavior for the level in the offer.
#ifdef RTC_ENABLE_H265
  if (media_description_options.type == webrtc::MediaType::VIDEO) {
    std::unordered_map<webrtc::H265Profile, webrtc::H265Level>
        supported_h265_profiles;
    // The assumption here is that H.265 codecs with the same profile and tier
    // are already with highest level for that profile in both
    // |supported_codecs| and |filtered_codecs|.
    for (const Codec& supported_codec : supported_codecs) {
      if (absl::EqualsIgnoreCase(supported_codec.name, kH265CodecName)) {
        std::optional<webrtc::H265ProfileTierLevel> supported_ptl =
            webrtc::ParseSdpForH265ProfileTierLevel(supported_codec.params);
        if (supported_ptl.has_value()) {
          supported_h265_profiles[supported_ptl->profile] =
              supported_ptl->level;
        }
      }
    }

    if (supported_h265_profiles.empty()) {
      return;
    }

    for (auto& filtered_codec : filtered_codecs) {
      if (absl::EqualsIgnoreCase(filtered_codec.name, kH265CodecName)) {
        std::optional<webrtc::H265ProfileTierLevel> filtered_ptl =
            webrtc::ParseSdpForH265ProfileTierLevel(filtered_codec.params);
        if (filtered_ptl.has_value()) {
          auto it = supported_h265_profiles.find(filtered_ptl->profile);

          if (it != supported_h265_profiles.end() &&
              filtered_ptl->level != it->second) {
            filtered_codec.params[kH265FmtpLevelId] =
                webrtc::H265LevelToString(it->second);
          }
        }
      }
    }
  }
#endif
}

RTCError NegotiateCodecs(const CodecList& local_codecs,
                         const CodecList& offered_codecs,
                         CodecList& negotiated_codecs_out,
                         bool keep_offer_order) {
  std::map<int, int> pt_mapping_table;
  // Since we build the negotiated codec list one entry at a time,
  // the list will have inconsistencies during building.
  std::vector<Codec> negotiated_codecs;
  for (const Codec& ours : local_codecs) {
    std::optional<Codec> theirs =
        FindMatchingCodec(local_codecs, offered_codecs, ours);
    // Note that we intentionally only find one matching codec for each of our
    // local codecs, in case the remote offer contains duplicate codecs.
    if (theirs) {
      Codec negotiated = ours;
      NegotiatePacketization(ours, *theirs, &negotiated);
      negotiated.IntersectFeedbackParams(*theirs);
      if (negotiated.GetResiliencyType() == Codec::ResiliencyType::kRtx) {
        // We support parsing the declarative rtx-time parameter.
        const auto rtx_time_it = theirs->params.find(kCodecParamRtxTime);
        if (rtx_time_it != theirs->params.end()) {
          negotiated.SetParam(kCodecParamRtxTime, rtx_time_it->second);
        }
      } else if (negotiated.GetResiliencyType() ==
                 Codec::ResiliencyType::kRed) {
        const auto red_it =
            theirs->params.find(kCodecParamNotInNameValueFormat);
        if (red_it != theirs->params.end()) {
          negotiated.SetParam(kCodecParamNotInNameValueFormat, red_it->second);
        }
      }
      if (absl::EqualsIgnoreCase(ours.name, kH264CodecName)) {
        webrtc::H264GenerateProfileLevelIdForAnswer(ours.params, theirs->params,
                                                    &negotiated.params);
      }
#ifdef RTC_ENABLE_H265
      if (absl::EqualsIgnoreCase(ours.name, kH265CodecName)) {
        webrtc::H265GenerateProfileTierLevelForAnswer(
            ours.params, theirs->params, &negotiated.params);
        NegotiateTxMode(ours, *theirs, &negotiated);
      }
#endif
      // Use their ID, if available.
      pt_mapping_table.insert({negotiated.id, theirs->id});
      negotiated.id = theirs->id;
      negotiated.name = theirs->name;
      negotiated_codecs.push_back(std::move(negotiated));
    }
  }
  // Fix up apt parameters that point to other PTs.
  for (Codec& negotiated : negotiated_codecs) {
    if (negotiated.GetResiliencyType() == Codec::ResiliencyType::kRtx) {
      // Change the apt value according to the pt mapping table.
      // This avoids changing to apt values that don't exist any more.
      std::string apt_str;
      if (!negotiated.GetParam(kCodecParamAssociatedPayloadType, &apt_str)) {
        RTC_LOG(LS_WARNING) << "No apt value";
        continue;
      }
      int apt_value;
      if (!rtc::FromString(apt_str, &apt_value)) {
        RTC_LOG(LS_WARNING) << "Unconvertable apt value";
        continue;
      }
      if (pt_mapping_table.count(apt_value) != 1) {
        RTC_LOG(LS_WARNING) << "Unmapped apt value " << apt_value;
        continue;
      }
      negotiated.SetParam(kCodecParamAssociatedPayloadType,
                          pt_mapping_table.at(apt_value));
    }
  }
  if (keep_offer_order) {
    // RFC3264: Although the answerer MAY list the formats in their desired
    // order of preference, it is RECOMMENDED that unless there is a
    // specific reason, the answerer list formats in the same relative order
    // they were present in the offer.
    // This can be skipped when the transceiver has any codec preferences.
    std::unordered_map<int, int> payload_type_preferences;
    int preference = static_cast<int>(offered_codecs.size() + 1);
    for (const Codec& codec : offered_codecs) {
      payload_type_preferences[codec.id] = preference--;
    }
    absl::c_sort(negotiated_codecs, [&payload_type_preferences](
                                        const Codec& a, const Codec& b) {
      return payload_type_preferences[a.id] > payload_type_preferences[b.id];
    });
  }
  RTCErrorOr<CodecList> result = CodecList::Create(negotiated_codecs);
  if (!result.ok()) {
    return result.MoveError();
  }
  negotiated_codecs_out = result.MoveValue();
  return RTCError::OK();
}

// Update the ID fields of the codec vector.
// If any codec has an ID with value "kIdNotSet", use the payload type suggester
// to assign and record a payload type for it.
// If there is a RED codec without its fmtp parameter, give it the ID of the
// first OPUS codec in the codec list.
webrtc::RTCError AssignCodecIdsAndLinkRed(
    webrtc::PayloadTypeSuggester* pt_suggester,
    const std::string& mid,
    std::vector<Codec>& codecs) {
  int codec_payload_type = Codec::kIdNotSet;
  for (cricket::Codec& codec : codecs) {
    if (codec.id == Codec::kIdNotSet) {
      // Add payload types to codecs, if needed
      // This should only happen if WebRTC-PayloadTypesInTransport field trial
      // is enabled.
      RTC_CHECK(pt_suggester);
      auto result = pt_suggester->SuggestPayloadType(mid, codec);
      if (!result.ok()) {
        return result.error();
      }
      codec.id = result.value();
    }
    // record first Opus codec id
    if (absl::EqualsIgnoreCase(codec.name, kOpusCodecName) &&
        codec_payload_type == Codec::kIdNotSet) {
      codec_payload_type = codec.id;
    }
  }
  if (codec_payload_type != Codec::kIdNotSet) {
    for (cricket::Codec& codec : codecs) {
      if (codec.type == Codec::Type::kAudio &&
          absl::EqualsIgnoreCase(codec.name, kRedCodecName)) {
        if (codec.params.empty()) {
          char buffer[100];
          rtc::SimpleStringBuilder param(buffer);
          param << codec_payload_type << "/" << codec_payload_type;
          codec.SetParam(kCodecParamNotInNameValueFormat, param.str());
        }
      }
    }
  }
  return webrtc::RTCError::OK();
}

}  // namespace

webrtc::RTCErrorOr<std::vector<Codec>> CodecVendor::GetNegotiatedCodecsForOffer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    const webrtc::ContentInfo* current_content,
    PayloadTypeSuggester& pt_suggester,
    const CodecList& codecs) {
  CodecList filtered_codecs;
  CodecList supported_codecs =
      media_description_options.type == webrtc::MediaType::AUDIO
          ? GetAudioCodecsForOffer(media_description_options.direction)
          : GetVideoCodecsForOffer(media_description_options.direction);

  if (media_description_options.codecs_to_include.empty()) {
    if (!media_description_options.codec_preferences.empty()) {
      // Add the codecs from the current transceiver's codec preferences.
      // They override any existing codecs from previous negotiations.
      filtered_codecs =
          MatchCodecPreference(media_description_options.codec_preferences,
                               codecs, supported_codecs);
    } else {
      // Add the codecs from current content if it exists and is not rejected
      // nor recycled.
      if (current_content && !current_content->rejected &&
          current_content->mid() == media_description_options.mid) {
        if (!IsMediaContentOfType(current_content,
                                  media_description_options.type)) {
          // Can happen if the remote side re-uses a MID while recycling.
          LOG_AND_RETURN_ERROR(RTCErrorType::INTERNAL_ERROR,
                               "Media type for content with mid='" +
                                   current_content->mid() +
                                   "' does not match previous type.");
        }
        const webrtc::MediaContentDescription* mcd =
            current_content->media_description();
        for (const Codec& codec : mcd->codecs()) {
          if (webrtc::FindMatchingCodec(mcd->codecs(), codecs.codecs(),
                                        codec)) {
            filtered_codecs.push_back(codec);
          }
        }
      }
      // Note what PTs are already in use.
      UsedPayloadTypes
          used_pltypes;  // Used to avoid pt collisions in filtered_codecs
      for (auto& codec : filtered_codecs) {
        // Note: This may change PTs. Doing so woud indicate an error, but
        // UsedPayloadTypes doesn't offer a means to make the distinction.
        used_pltypes.FindAndSetIdUsed(&codec);
      }
      // Add other supported codecs.
      for (const Codec& codec : supported_codecs) {
        std::optional<Codec> found_codec =
            FindMatchingCodec(supported_codecs, codecs, codec);
        if (found_codec &&
            !FindMatchingCodec(supported_codecs, filtered_codecs, codec)) {
          // Use the `found_codec` from `codecs` because it has the
          // correctly mapped payload type (most of the time).
          if (media_description_options.type == webrtc::MediaType::VIDEO &&
              found_codec->GetResiliencyType() == Codec::ResiliencyType::kRtx) {
            // For RTX we might need to adjust the apt parameter if we got a
            // remote offer without RTX for a codec for which we support RTX.
            // This is only done for video since we do not yet have rtx for
            // audio.
            auto referenced_codec =
                GetAssociatedCodecForRtx(supported_codecs, codec);
            RTC_DCHECK(referenced_codec);

            // Find the codec we should be referencing and point to it.
            std::optional<Codec> changed_referenced_codec = FindMatchingCodec(
                supported_codecs, filtered_codecs, *referenced_codec);
            if (changed_referenced_codec) {
              found_codec->SetParam(kCodecParamAssociatedPayloadType,
                                    changed_referenced_codec->id);
            }
          }
          // Quick fix for b/395077842: Remap the codec if it collides.
          used_pltypes.FindAndSetIdUsed(&(*found_codec));
          filtered_codecs.push_back(*found_codec);
        }
      }
    }

    if (media_description_options.type == webrtc::MediaType::AUDIO &&
        !session_options.vad_enabled) {
      // If application doesn't want CN codecs in offer.
      StripCNCodecs(filtered_codecs);
    } else if (media_description_options.type == webrtc::MediaType::VIDEO &&
               session_options.raw_packetization_for_video) {
      for (Codec& codec : filtered_codecs) {
        if (codec.IsMediaCodec()) {
          codec.packetization = kPacketizationParamRaw;
        }
      }
    }
    NegotiateVideoCodecLevelsForOffer(media_description_options,
                                      supported_codecs, filtered_codecs);
  } else {
    // media_description_options.codecs_to_include contains codecs
    // TODO: issues.webrtc.org/360058654 - figure out if this can be deleted.
    RTCErrorOr<CodecList> codecs_from_arg =
        CodecList::Create(media_description_options.codecs_to_include);
    if (!codecs_from_arg.ok()) {
      return codecs_from_arg.MoveError();
    }
    filtered_codecs = codecs_from_arg.MoveValue();
  }
  AssignCodecIdsAndLinkRed(&pt_suggester, media_description_options.mid,
                           filtered_codecs.writable_codecs());
  return filtered_codecs.codecs();
}

webrtc::RTCErrorOr<Codecs> CodecVendor::GetNegotiatedCodecsForAnswer(
    const MediaDescriptionOptions& media_description_options,
    const MediaSessionOptions& session_options,
    webrtc::RtpTransceiverDirection offer_rtd,
    webrtc::RtpTransceiverDirection answer_rtd,
    const webrtc::ContentInfo* current_content,
    const std::vector<Codec> codecs_from_offer,
    PayloadTypeSuggester& pt_suggester,
    const CodecList& codecs) {
  CodecList filtered_codecs;
  CodecList negotiated_codecs;
  if (media_description_options.codecs_to_include.empty()) {
    const CodecList& supported_codecs =
        media_description_options.type == webrtc::MediaType::AUDIO
            ? GetAudioCodecsForAnswer(offer_rtd, answer_rtd)
            : GetVideoCodecsForAnswer(offer_rtd, answer_rtd);
    if (!media_description_options.codec_preferences.empty()) {
      filtered_codecs =
          MatchCodecPreference(media_description_options.codec_preferences,
                               codecs, supported_codecs);
    } else {
      // Add the codecs from current content if it exists and is not rejected
      // nor recycled.
      if (current_content && !current_content->rejected &&
          current_content->mid() == media_description_options.mid) {
        if (!IsMediaContentOfType(current_content,
                                  media_description_options.type)) {
          // Can happen if the remote side re-uses a MID while recycling.
          LOG_AND_RETURN_ERROR(RTCErrorType::INTERNAL_ERROR,
                               "Media type for content with mid='" +
                                   current_content->mid() +
                                   "' does not match previous type.");
        }
        const webrtc::MediaContentDescription* mcd =
            current_content->media_description();
        for (const Codec& codec : mcd->codecs()) {
          if (webrtc::FindMatchingCodec(mcd->codecs(), codecs.codecs(),
                                        codec)) {
            filtered_codecs.push_back(codec);
          }
        }
      }
      // Add other supported codecs.
      CodecList other_codecs;
      for (const Codec& codec : supported_codecs) {
        if (FindMatchingCodec(supported_codecs, codecs, codec) &&
            !FindMatchingCodec(supported_codecs, filtered_codecs, codec)) {
          // We should use the local codec with local parameters and the codec
          // id would be correctly mapped in `NegotiateCodecs`.
          other_codecs.push_back(codec);
        }
      }

      // Use ComputeCodecsUnion to avoid having duplicate payload IDs.
      // This is a no-op for audio until RTX is added.
      filtered_codecs = ComputeCodecsUnion(filtered_codecs, other_codecs);
    }

    if (media_description_options.type == webrtc::MediaType::AUDIO &&
        !session_options.vad_enabled) {
      // If application doesn't want CN codecs in offer.
      StripCNCodecs(filtered_codecs);
    } else if (media_description_options.type == webrtc::MediaType::VIDEO &&
               session_options.raw_packetization_for_video) {
      for (Codec& codec : filtered_codecs) {
        if (codec.IsMediaCodec()) {
          codec.packetization = kPacketizationParamRaw;
        }
      }
    }
    // An offer is external data, so needs to be checked before use.
    auto checked_codecs_from_offer = CodecList::Create(codecs_from_offer);
    if (!checked_codecs_from_offer.ok()) {
      return checked_codecs_from_offer.MoveError();
    }
    NegotiateCodecs(filtered_codecs, checked_codecs_from_offer.value(),
                    negotiated_codecs,
                    media_description_options.codec_preferences.empty());
  } else {
    // media_description_options.codecs_to_include contains codecs
    RTCErrorOr<CodecList> codecs_from_arg =
        CodecList::Create(media_description_options.codecs_to_include);
    if (!codecs_from_arg.ok()) {
      return codecs_from_arg.MoveError();
    }
    negotiated_codecs = codecs_from_arg.MoveValue();
  }
  AssignCodecIdsAndLinkRed(&pt_suggester, media_description_options.mid,
                           negotiated_codecs.writable_codecs());
  return negotiated_codecs.codecs();
}

CodecVendor::CodecVendor(MediaEngineInterface* media_engine,
                         bool rtx_enabled,
                         const webrtc::FieldTrialsView&
                             trials) {  // Null media_engine is permitted in
                                        // order to allow unit testing where
  // the codecs are explicitly set by the test.
  if (media_engine) {
    audio_send_codecs_ =
        TypedCodecVendor(media_engine, webrtc::MediaType::AUDIO,
                         /* is_sender= */ true, rtx_enabled, trials);
    audio_recv_codecs_ =
        TypedCodecVendor(media_engine, webrtc::MediaType::AUDIO,
                         /* is_sender= */ false, rtx_enabled, trials);
    video_send_codecs_ =
        TypedCodecVendor(media_engine, webrtc::MediaType::VIDEO,
                         /* is_sender= */ true, rtx_enabled, trials);
    video_recv_codecs_ =
        TypedCodecVendor(media_engine, webrtc::MediaType::VIDEO,
                         /* is_sender= */ false, rtx_enabled, trials);
  }
}

const CodecList& CodecVendor::audio_send_codecs() const {
  return audio_send_codecs_.codecs();
}

const CodecList& CodecVendor::audio_recv_codecs() const {
  return audio_recv_codecs_.codecs();
}

void CodecVendor::set_audio_codecs(const CodecList& send_codecs,
                                   const CodecList& recv_codecs) {
  audio_send_codecs_.set_codecs(send_codecs);
  audio_recv_codecs_.set_codecs(recv_codecs);
}

const CodecList& CodecVendor::video_send_codecs() const {
  return video_send_codecs_.codecs();
}

const CodecList& CodecVendor::video_recv_codecs() const {
  return video_recv_codecs_.codecs();
}

void CodecVendor::set_video_codecs(const CodecList& send_codecs,
                                   const CodecList& recv_codecs) {
  video_send_codecs_.set_codecs(send_codecs);
  video_recv_codecs_.set_codecs(recv_codecs);
}
// Getting codecs for an offer involves these steps:
//
// 1. Construct payload type -> codec mappings for current description.
// 2. Add any reference codecs that weren't already present
// 3. For each individual media description (m= section), filter codecs based
//    on the directional attribute (happens in another method).
RTCError CodecVendor::GetCodecsForOffer(
    const std::vector<const webrtc::ContentInfo*>& current_active_contents,
    CodecList& audio_codecs,
    CodecList& video_codecs) const {
  // First - get all codecs from the current description if the media type
  // is used. Add them to `used_pltypes` so the payload type is not reused if a
  // new media type is added.
  UsedPayloadTypes used_pltypes;
  auto error = MergeCodecsFromDescription(current_active_contents, audio_codecs,
                                          video_codecs, &used_pltypes);
  if (!error.ok()) {
    return error;
  }
  // Add our codecs that are not in the current description.
  MergeCodecs(all_audio_codecs(), audio_codecs, &used_pltypes);
  MergeCodecs(all_video_codecs(), video_codecs, &used_pltypes);
  return RTCError::OK();
}

// Getting codecs for an answer involves these steps:
//
// 1. Construct payload type -> codec mappings for current description.
// 2. Add any codecs from the offer that weren't already present.
// 3. Add any remaining codecs that weren't already present.
// 4. For each individual media description (m= section), filter codecs based
//    on the directional attribute (happens in another method).
RTCError CodecVendor::GetCodecsForAnswer(
    const std::vector<const webrtc::ContentInfo*>& current_active_contents,
    const webrtc::SessionDescription& remote_offer,
    CodecList& audio_codecs,
    CodecList& video_codecs) const {
  // First - get all codecs from the current description if the media type
  // is used. Add them to `used_pltypes` so the payload type is not reused if a
  // new media type is added.
  UsedPayloadTypes used_pltypes;
  RTCError error = MergeCodecsFromDescription(
      current_active_contents, audio_codecs, video_codecs, &used_pltypes);
  if (!error.ok()) {
    return error;
  }
  // Second - filter out codecs that we don't support at all and should ignore.
  CodecList filtered_offered_audio_codecs;
  CodecList filtered_offered_video_codecs;
  for (const ContentInfo& content : remote_offer.contents()) {
    RTCErrorOr<CodecList> offered_codecs =
        CodecList::Create(content.media_description()->codecs());
    if (!offered_codecs.ok()) {
      return offered_codecs.MoveError();
    }
    if (IsMediaContentOfType(&content, webrtc::MediaType::AUDIO)) {
      for (const Codec& offered_audio_codec : offered_codecs.value()) {
        if (!FindMatchingCodec(offered_codecs.value(),
                               filtered_offered_audio_codecs,
                               offered_audio_codec) &&
            FindMatchingCodec(offered_codecs.value(), all_audio_codecs(),
                              offered_audio_codec)) {
          filtered_offered_audio_codecs.push_back(offered_audio_codec);
        }
      }
    } else if (IsMediaContentOfType(&content, webrtc::MediaType::VIDEO)) {
      std::vector<Codec> pending_rtx_codecs;
      for (const Codec& offered_video_codec : offered_codecs.value()) {
        if (!FindMatchingCodec(offered_codecs.value(),
                               filtered_offered_video_codecs,
                               offered_video_codec) &&
            FindMatchingCodec(offered_codecs.value(), all_video_codecs(),
                              offered_video_codec)) {
          // Special case: If it's an RTX codec, and the APT points to
          // a codec that is not yet in the codec list, put it aside.
          if (offered_video_codec.GetResiliencyType() ==
                  Codec::ResiliencyType::kRtx &&
              !GetAssociatedCodecForRtx(filtered_offered_video_codecs,
                                        offered_video_codec)) {
            pending_rtx_codecs.push_back(offered_video_codec);
            continue;
          }
          filtered_offered_video_codecs.push_back(offered_video_codec);
        }
      }
      // If the associated codec showed up later in the codec list,
      // append the corresponding RTX codec.
      for (const Codec& codec : pending_rtx_codecs) {
        if (GetAssociatedCodecForRtx(filtered_offered_video_codecs, codec)) {
          filtered_offered_video_codecs.push_back(codec);
        }
      }
    }
  }

  // Add codecs that are not in the current description but were in
  // `remote_offer`.
  MergeCodecs(filtered_offered_audio_codecs, audio_codecs, &used_pltypes);
  MergeCodecs(filtered_offered_video_codecs, video_codecs, &used_pltypes);
  return RTCError::OK();
}

CodecList CodecVendor::GetVideoCodecsForOffer(
    const RtpTransceiverDirection& direction) const {
  switch (direction) {
    // If stream is inactive - generate list as if sendrecv.
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      return video_sendrecv_codecs();
    case RtpTransceiverDirection::kSendOnly:
      return video_send_codecs_.codecs();
    case RtpTransceiverDirection::kRecvOnly:
      return video_recv_codecs_.codecs();
  }
  RTC_CHECK_NOTREACHED();
}

CodecList CodecVendor::GetVideoCodecsForAnswer(
    const RtpTransceiverDirection& offer,
    const RtpTransceiverDirection& answer) const {
  switch (answer) {
    // For inactive and sendrecv answers, generate lists as if we were to accept
    // the offer's direction. See RFC 3264 Section 6.1.
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      return GetVideoCodecsForOffer(
          webrtc::RtpTransceiverDirectionReversed(offer));
    case RtpTransceiverDirection::kSendOnly:
      return video_send_codecs_.codecs();
    case RtpTransceiverDirection::kRecvOnly:
      return video_recv_codecs_.codecs();
  }
  RTC_CHECK_NOTREACHED();
}

CodecList CodecVendor::GetAudioCodecsForOffer(
    const RtpTransceiverDirection& direction) const {
  switch (direction) {
    // If stream is inactive - generate list as if sendrecv.
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      return audio_sendrecv_codecs();
    case RtpTransceiverDirection::kSendOnly:
      return audio_send_codecs_.codecs();
    case RtpTransceiverDirection::kRecvOnly:
      return audio_recv_codecs_.codecs();
  }
  RTC_CHECK_NOTREACHED();
}

CodecList CodecVendor::GetAudioCodecsForAnswer(
    const RtpTransceiverDirection& offer,
    const RtpTransceiverDirection& answer) const {
  switch (answer) {
    // For inactive and sendrecv answers, generate lists as if we were to accept
    // the offer's direction. See RFC 3264 Section 6.1.
    case RtpTransceiverDirection::kSendRecv:
    case RtpTransceiverDirection::kStopped:
    case RtpTransceiverDirection::kInactive:
      return GetAudioCodecsForOffer(
          webrtc::RtpTransceiverDirectionReversed(offer));
    case RtpTransceiverDirection::kSendOnly:
      return audio_send_codecs_.codecs();
    case RtpTransceiverDirection::kRecvOnly:
      return audio_recv_codecs_.codecs();
  }
  RTC_CHECK_NOTREACHED();
}

CodecList CodecVendor::all_video_codecs() const {
  // Use ComputeCodecsUnion to avoid having duplicate payload IDs
  return ComputeCodecsUnion(video_recv_codecs_.codecs(),
                            video_send_codecs_.codecs());
}

CodecList CodecVendor::all_audio_codecs() const {
  // Compute the audio codecs union.
  CodecList codecs;
  for (const Codec& send : audio_send_codecs_.codecs()) {
    codecs.push_back(send);
    if (!FindMatchingCodec(audio_send_codecs_.codecs(),
                           audio_recv_codecs_.codecs(), send)) {
      // It doesn't make sense to have an RTX codec we support sending but not
      // receiving.
      RTC_DCHECK(send.GetResiliencyType() != Codec::ResiliencyType::kRtx);
    }
  }
  for (const Codec& recv : audio_recv_codecs_.codecs()) {
    if (!FindMatchingCodec(audio_recv_codecs_.codecs(),
                           audio_send_codecs_.codecs(), recv)) {
      codecs.push_back(recv);
    }
  }
  return codecs;
}

CodecList CodecVendor::audio_sendrecv_codecs() const {
  // Use NegotiateCodecs to merge our codec lists, since the operation is
  // essentially the same. Put send_codecs as the offered_codecs, which is the
  // order we'd like to follow. The reasoning is that encoding is usually more
  // expensive than decoding, and prioritizing a codec in the send list probably
  // means it's a codec we can handle efficiently.
  CodecList audio_sendrecv_codecs;
  auto error =
      NegotiateCodecs(audio_recv_codecs_.codecs(), audio_send_codecs_.codecs(),
                      audio_sendrecv_codecs, true);
  RTC_DCHECK(error.ok());
  return audio_sendrecv_codecs;
}

CodecList CodecVendor::video_sendrecv_codecs() const {
  // Use NegotiateCodecs to merge our codec lists, since the operation is
  // essentially the same. Put send_codecs as the offered_codecs, which is the
  // order we'd like to follow. The reasoning is that encoding is usually more
  // expensive than decoding, and prioritizing a codec in the send list probably
  // means it's a codec we can handle efficiently.
  // Also for the same profile of a codec, if there are different levels in the
  // send and receive codecs, |video_sendrecv_codecs| will contain the lower
  // level of the two for that profile.
  CodecList video_sendrecv_codecs;
  auto error =
      NegotiateCodecs(video_recv_codecs_.codecs(), video_send_codecs_.codecs(),
                      video_sendrecv_codecs, true);
  RTC_DCHECK(error.ok());
  return video_sendrecv_codecs;
}

}  // namespace cricket
