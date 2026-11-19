/*
 * SampleDecoder.cpp - Decodes audio files in various formats
 *
 * Copyright (c) 2023 saker <sakertooth@gmail.com>
 *
 * This file is part of MXM (Musica ex Machina), a fork of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "SampleDecoder.h"

#include <QFile>
#include <QString>
#include <algorithm>
#include <array>
#include <memory>
#include <sndfile.h>

#ifdef MXM_HAVE_OGGVORBIS
#include <vorbis/vorbisfile.h>
#endif

#include "AudioEngine.h"
#include "DrumSynth.h"
#include "Engine.h"
#include "MxmTypes.h"

namespace mxm {

namespace {

using Decoder = std::optional<SampleDecoder::Result> (*)(const QString&);

auto decodeSampleSF(const QString& audioFile) -> std::optional<SampleDecoder::Result>;
auto decodeSampleDS(const QString& audioFile) -> std::optional<SampleDecoder::Result>;
#ifdef MXM_HAVE_OGGVORBIS
auto decodeSampleOggVorbis(const QString& audioFile) -> std::optional<SampleDecoder::Result>;
#endif

static constexpr std::array<Decoder, 3> decoders = {&decodeSampleSF,
#ifdef MXM_HAVE_OGGVORBIS
	&decodeSampleOggVorbis,
#endif
	&decodeSampleDS};

auto channelLayoutFromSndfile(SNDFILE* sndFile, ch_cnt_t channels) -> AudioChannelLayout
{
	if (channels == 0) { return AudioChannelLayout::discrete(0); }
	if (channels == 1) { return AudioChannelLayout::mono(); }
	if (channels == 2) { return AudioChannelLayout::stereo(); }

	auto map = std::array<int, MaxChannelsPerAudioBuffer>{};
	if (sf_command(sndFile, SFC_GET_CHANNEL_MAP_INFO, map.data(), channels * sizeof(int)) != SF_TRUE)
	{
		// A file without an explicit channel map is a discrete channel set, not
		// necessarily a standard 5.1 or 7.1 speaker layout.
		return AudioChannelLayout::discrete(channels);
	}

	auto roles = AudioChannelLayout::Roles{};
	for (ch_cnt_t channel = 0; channel < channels; ++channel)
	{
		switch (map[channel])
		{
		case SF_CHANNEL_MAP_MONO:
		case SF_CHANNEL_MAP_CENTER:
		case SF_CHANNEL_MAP_FRONT_CENTER:
			roles[channel] = SpeakerRole::FrontCenter;
			break;
		case SF_CHANNEL_MAP_LEFT:
		case SF_CHANNEL_MAP_FRONT_LEFT:
			roles[channel] = SpeakerRole::FrontLeft;
			break;
		case SF_CHANNEL_MAP_RIGHT:
		case SF_CHANNEL_MAP_FRONT_RIGHT:
			roles[channel] = SpeakerRole::FrontRight;
			break;
		case SF_CHANNEL_MAP_REAR_LEFT:
			roles[channel] = SpeakerRole::BackLeft;
			break;
		case SF_CHANNEL_MAP_REAR_RIGHT:
			roles[channel] = SpeakerRole::BackRight;
			break;
		case SF_CHANNEL_MAP_REAR_CENTER:
			roles[channel] = SpeakerRole::BackCenter;
			break;
		case SF_CHANNEL_MAP_LFE:
			roles[channel] = SpeakerRole::Lfe;
			break;
		case SF_CHANNEL_MAP_FRONT_LEFT_OF_CENTER:
			roles[channel] = SpeakerRole::FrontLeftOfCenter;
			break;
		case SF_CHANNEL_MAP_FRONT_RIGHT_OF_CENTER:
			roles[channel] = SpeakerRole::FrontRightOfCenter;
			break;
		case SF_CHANNEL_MAP_SIDE_LEFT:
			roles[channel] = SpeakerRole::SideLeft;
			break;
		case SF_CHANNEL_MAP_SIDE_RIGHT:
			roles[channel] = SpeakerRole::SideRight;
			break;
		case SF_CHANNEL_MAP_TOP_FRONT_LEFT:
			roles[channel] = SpeakerRole::TopFrontLeft;
			break;
		case SF_CHANNEL_MAP_TOP_FRONT_RIGHT:
			roles[channel] = SpeakerRole::TopFrontRight;
			break;
		case SF_CHANNEL_MAP_TOP_REAR_LEFT:
			roles[channel] = SpeakerRole::TopBackLeft;
			break;
		case SF_CHANNEL_MAP_TOP_REAR_RIGHT:
			roles[channel] = SpeakerRole::TopBackRight;
			break;
		default:
			roles[channel] = SpeakerRole::Unknown;
			break;
		}
	}
	return AudioChannelLayout{channels, roles};
}

auto decodeSampleSF(const QString& audioFile) -> std::optional<SampleDecoder::Result>
{
	SNDFILE* sndFile = nullptr;
	auto sfInfo = SF_INFO{};

	// TODO: Remove use of QFile
	auto file = QFile{audioFile};
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }

	sndFile = sf_open_fd(file.handle(), SFM_READ, &sfInfo, false);
	if (sndFile == nullptr || sf_error(sndFile) != 0) { return std::nullopt; }

	if (sfInfo.channels <= 0 || sfInfo.channels > MaxChannelsPerAudioBuffer || sfInfo.frames < 0
		|| sfInfo.samplerate <= 0)
	{
		sf_close(sndFile);
		return std::nullopt;
	}

	const auto channels = static_cast<ch_cnt_t>(sfInfo.channels);
	auto samples = std::vector<float>(static_cast<std::size_t>(sfInfo.frames) * channels);
	const auto framesRead = sf_readf_float(sndFile, samples.data(), sfInfo.frames);
	const auto layout = channelLayoutFromSndfile(sndFile, channels);

	sf_close(sndFile);
	file.close();

	if (framesRead < 0) { return std::nullopt; }
	samples.resize(static_cast<std::size_t>(framesRead) * channels);

	return SampleDecoder::Result{std::move(samples), channels, layout, static_cast<int>(sfInfo.samplerate)};
}

auto decodeSampleDS(const QString& audioFile) -> std::optional<SampleDecoder::Result>
{
	// Populated by DrumSynth::GetDSFileSamples
	int_sample_t* dataPtr = nullptr;

	auto ds = DrumSynth{};
	const auto engineRate = Engine::audioEngine()->outputSampleRate();
	const auto frames = ds.GetDSFileSamples(audioFile, dataPtr, DEFAULT_CHANNELS, engineRate);
	const auto data = std::unique_ptr<int_sample_t[]>{dataPtr}; // NOLINT, we have to use a C-style array here

	if (frames <= 0 || !data) { return std::nullopt; }

	auto samples = std::vector<float>(static_cast<std::size_t>(frames) * DEFAULT_CHANNELS);
	src_short_to_float_array(data.get(), samples.data(), frames * DEFAULT_CHANNELS);

	return SampleDecoder::Result{
		std::move(samples), DEFAULT_CHANNELS, AudioChannelLayout::stereo(), static_cast<int>(engineRate)};
}

#ifdef MXM_HAVE_OGGVORBIS
auto decodeSampleOggVorbis(const QString& audioFile) -> std::optional<SampleDecoder::Result>
{
	static auto s_read = [](void* buffer, size_t size, size_t count, void* stream) -> size_t {
		auto file = static_cast<QFile*>(stream);
		return file->read(static_cast<char*>(buffer), size * count);
	};

	static auto s_seek = [](void* stream, ogg_int64_t offset, int whence) -> int {
		auto file = static_cast<QFile*>(stream);
		if (whence == SEEK_SET) { file->seek(offset); }
		else if (whence == SEEK_CUR) { file->seek(file->pos() + offset); }
		else if (whence == SEEK_END) { file->seek(file->size() + offset); }
		else { return -1; }
		return 0;
	};

	static auto s_close = [](void* stream) -> int {
		auto file = static_cast<QFile*>(stream);
		file->close();
		return 0;
	};

	static auto s_tell = [](void* stream) -> long {
		auto file = static_cast<QFile*>(stream);
		return file->pos();
	};

	static ov_callbacks s_callbacks = {s_read, s_seek, s_close, s_tell};

	// TODO: Remove use of QFile
	auto file = QFile{audioFile};
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }

	auto vorbisFile = OggVorbis_File{};
	if (ov_open_callbacks(&file, &vorbisFile, nullptr, 0, s_callbacks) < 0) { return std::nullopt; }

	const auto vorbisInfo = ov_info(&vorbisFile, -1);
	if (vorbisInfo == nullptr) { return std::nullopt; }

	const auto channels = static_cast<ch_cnt_t>(vorbisInfo->channels);
	const auto sampleRate = vorbisInfo->rate;
	const auto numSamples = ov_pcm_total(&vorbisFile, -1);
	if (channels == 0 || channels > MaxChannelsPerAudioBuffer || sampleRate <= 0 || numSamples < 0)
	{
		ov_clear(&vorbisFile);
		return std::nullopt;
	}

	const auto channelMap = SampleDecoder::vorbisChannelMap(channels);
	auto samples = std::vector<float>{};
	if (numSamples > 0) { samples.reserve(static_cast<std::size_t>(numSamples) * channels); }
	auto output = static_cast<float**>(nullptr);

	while (true)
	{
		const auto framesRead = ov_read_float(&vorbisFile, &output, 4096, nullptr);

		if (framesRead < 0)
		{
			ov_clear(&vorbisFile);
			return std::nullopt;
		}
		else if (framesRead == 0) { break; }

		for (long frame = 0; frame < framesRead; ++frame)
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				const auto source = channels <= 8 ? channelMap.order[channel] : channel;
				samples.push_back(output[source][frame]);
			}
		}
	}

	ov_clear(&vorbisFile);
	return SampleDecoder::Result{
		std::move(samples), channels, channelMap.layout, static_cast<int>(sampleRate)};
}
#endif // MXM_HAVE_OGGVORBIS
} // namespace

auto SampleDecoder::vorbisChannelMap(ch_cnt_t channels) -> VorbisChannelMap
{
	auto map = VorbisChannelMap{};
	map.layout = AudioChannelLayout::discrete(channels);
	for (ch_cnt_t channel = 0; channel < std::min<ch_cnt_t>(channels, 8); ++channel)
	{
		map.order[channel] = channel;
	}

	auto roles = AudioChannelLayout::Roles{};
	switch (channels)
	{
	case 1:
		map.layout = AudioChannelLayout::mono();
		map.order[0] = 0;
		break;
	case 2:
		map.layout = AudioChannelLayout::stereo();
		map.order[0] = 0;
		map.order[1] = 1;
		break;
	case 3:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		map.layout = AudioChannelLayout{3, roles};
		map.order = {0, 2, 1};
		break;
	case 4:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::BackLeft;
		roles[3] = SpeakerRole::BackRight;
		map.layout = AudioChannelLayout{4, roles};
		map.order = {0, 1, 2, 3};
		break;
	case 5:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::BackLeft;
		roles[4] = SpeakerRole::BackRight;
		map.layout = AudioChannelLayout{5, roles};
		map.order = {0, 2, 1, 3, 4};
		break;
	case 6:
		map.layout = AudioChannelLayout::surround51();
		map.order = {0, 2, 1, 5, 3, 4};
		break;
	case 7:
		roles[0] = SpeakerRole::FrontLeft;
		roles[1] = SpeakerRole::FrontRight;
		roles[2] = SpeakerRole::FrontCenter;
		roles[3] = SpeakerRole::Lfe;
		roles[4] = SpeakerRole::SideLeft;
		roles[5] = SpeakerRole::SideRight;
		roles[6] = SpeakerRole::BackCenter;
		map.layout = AudioChannelLayout{7, roles};
		map.order = {0, 2, 1, 6, 3, 4, 5};
		break;
	case 8:
		map.layout = AudioChannelLayout::surround71();
		map.order = {0, 2, 1, 7, 5, 6, 3, 4};
		break;
	default:
		break;
	}
	return map;
}

auto SampleDecoder::supportedAudioTypes() -> const std::vector<AudioType>&
{
	static const auto s_audioTypes = [] {
		auto types = std::vector<AudioType>();

		// Add DrumSynth by default since that support comes from us
		types.push_back(AudioType{"DrumSynth", "ds"});

		auto sfFormatInfo = SF_FORMAT_INFO{};
		auto simpleTypeCount = 0;
		sf_command(nullptr, SFC_GET_SIMPLE_FORMAT_COUNT, &simpleTypeCount, sizeof(int));

		// TODO: Ideally, this code should be iterating over the major formats, but some important extensions such as
		// *.ogg are not included. This is planned for future versions of sndfile.
		for (int simple = 0; simple < simpleTypeCount; ++simple)
		{
			sfFormatInfo.format = simple;
			sf_command(nullptr, SFC_GET_SIMPLE_FORMAT, &sfFormatInfo, sizeof(sfFormatInfo));

			auto it = std::find_if(types.begin(), types.end(),
				[&](const AudioType& type) { return sfFormatInfo.extension == type.extension; });
			if (it != types.end()) { continue; }

			auto name = std::string{sfFormatInfo.extension};
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return std::toupper(ch); });

			types.push_back(AudioType{std::move(name), sfFormatInfo.extension});
		}

		std::sort(types.begin(), types.end(), [&](const AudioType& a, const AudioType& b) { return a.name < b.name; });
		return types;
	}();
	return s_audioTypes;
}

auto SampleDecoder::decode(const QString& audioFile) -> std::optional<Result>
{
	auto result = std::optional<Result>{};
	for (const auto& decoder : decoders)
	{
		result = decoder(audioFile);
		if (result) { break; }
	}

	return result;
}

} // namespace mxm
