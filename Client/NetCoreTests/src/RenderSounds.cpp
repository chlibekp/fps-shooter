// Writes every procedural sound (all variants) as WAV files for listening
// and review: tfps_render_sounds <output directory>
#include "Audio/TfpsSynth.h"

#include <cstdio>
#include <string>

int main(int Argc, char** Argv)
{
	if (Argc != 2)
	{
		std::fprintf(stderr, "usage: %s <output directory>\n", Argv[0]);
		return 2;
	}
	int Files = 0;
	double Seconds = 0.0;
	for (int C = 0; C < tfps::audio::kCueCount; ++C)
	{
		const auto Id = static_cast<tfps::audio::Cue>(C);
		const tfps::audio::CueInfo& I = tfps::audio::Info(Id);
		for (int V = 0; V < I.Variants; ++V)
		{
			const std::vector<int16_t> Pcm = tfps::audio::Render(Id, static_cast<uint32_t>(V));
			const std::vector<uint8_t> Wav = tfps::audio::EncodeWav(Pcm);
			const std::string Path = std::string(Argv[1]) + "/" + I.Name + (I.Variants > 1 ? "_" + std::to_string(V) : std::string()) + ".wav";
			FILE* F = std::fopen(Path.c_str(), "wb");
			if (F == nullptr || std::fwrite(Wav.data(), 1, Wav.size(), F) != Wav.size())
			{
				std::fprintf(stderr, "cannot write %s\n", Path.c_str());
				return 1;
			}
			std::fclose(F);
			++Files;
			Seconds += static_cast<double>(Pcm.size()) / tfps::audio::kSampleRate;
		}
	}
	std::printf("wrote %d files (%.1f s of audio) to %s\n", Files, Seconds, Argv[1]);
	return 0;
}
