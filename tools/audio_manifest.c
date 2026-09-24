// Build tool, run under Node (NODERAWFS): lists the game's sound files with what the
// browser's FMOD needs to know about them before their bytes arrive, and copies each
// one into the site under a name made from its contents, so it can be cached forever.
//
//   audio_manifest <data directory> <site audio directory> <engine path prefix>
//
// For every .flac, .ogg, .wav and .mp3 under the data directory, manifest.tsv in the
// site audio directory gets one line, sorted by path:
//   <path as the engine names it>\t<bytes>\t<frames>\t<sample rate>\t<site file name>
// The frames and sample rate come from miniaudio's decoder exactly as
// FMOD::System::createSound reads them from the file (runtime/fmod/sound.cpp). Files in
// the site audio directory that the manifest no longer names are removed.
#include "miniaudio.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
	char** items;
	size_t count;
	size_t capacity;
} List;

static void Append(List* list, const char* text) {
	if (list->count == list->capacity) {
		list->capacity = list->capacity ? list->capacity * 2 : 256;
		list->items = realloc(list->items, list->capacity * sizeof(char*));
	}
	list->items[list->count++] = strdup(text);
}

static int Compare(const void* a, const void* b) {
	return strcmp(*(char* const*)a, *(char* const*)b);
}

static const char* AudioExtension(const char* name) {
	const char* dot = strrchr(name, '.');
	if (!dot) {
		return NULL;
	}
	static const char* const extensions[] = {".flac", ".ogg", ".wav", ".mp3"};
	for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
		if (strcmp(dot, extensions[i]) == 0) {
			return dot;
		}
	}
	return NULL;
}

// Every sound file below root, as paths relative to it. The paths live on the heap:
// Emscripten's default stack is 64 KB.
static void Collect(const char* root, const char* relative, List* found) {
	char* directory = malloc(8192);
	char* child = directory + 4096;
	snprintf(directory, 4096, "%s%s%s", root, *relative ? "/" : "", relative);
	DIR* listing = opendir(directory);
	if (!listing) {
		free(directory);
		return;
	}
	struct dirent* entry;
	while ((entry = readdir(listing))) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		snprintf(child, 4096, "%s%s%s", relative, *relative ? "/" : "", entry->d_name);
		snprintf(directory, 4096, "%s/%s", root, child);
		struct stat status;
		if (stat(directory, &status) != 0) {
			continue;
		}
		if (S_ISDIR(status.st_mode)) {
			Collect(root, child, found);
		} else if (AudioExtension(entry->d_name)) {
			Append(found, child);
		}
	}
	closedir(listing);
	free(directory);
}

static unsigned char* ReadAll(const char* path, size_t* size) {
	FILE* file = fopen(path, "rb");
	if (!file) {
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	unsigned char* bytes = malloc(length > 0 ? (size_t)length : 1);
	*size = fread(bytes, 1, (size_t)length, file);
	fclose(file);
	return bytes;
}

// FNV-1a, 64 bits: only has to tell versions of a file apart.
static uint64_t Hash(const unsigned char* bytes, size_t size) {
	uint64_t hash = 14695981039346656037ULL;
	for (size_t i = 0; i < size; ++i) {
		hash ^= bytes[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

int main(int argc, char** argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: audio_manifest <data directory> <site audio directory> <engine path prefix>\n");
		return 2;
	}
	const char* dataDirectory = argv[1];
	const char* siteDirectory = argv[2];
	const char* prefix = argv[3];
	mkdir(siteDirectory, 0755);

	List sounds = {0};
	Collect(dataDirectory, "", &sounds);
	qsort(sounds.items, sounds.count, sizeof(char*), Compare);

	char manifestPath[4096];
	snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.tsv.tmp", siteDirectory);
	FILE* manifest = fopen(manifestPath, "w");
	if (!manifest) {
		fprintf(stderr, "cannot write %s\n", manifestPath);
		return 1;
	}
	List written = {0};
	uint64_t totalBytes = 0;
	for (size_t i = 0; i < sounds.count; ++i) {
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s", dataDirectory, sounds.items[i]);
		size_t size = 0;
		unsigned char* bytes = ReadAll(path, &size);
		if (!bytes) {
			fprintf(stderr, "cannot read %s\n", path);
			return 1;
		}
		// What createSound reads from the file's header, read the same way.
		ma_decoder decoder;
		ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_uint64 frames = 0;
		if (ma_decoder_init_file(path, &config, &decoder) != MA_SUCCESS) {
			fprintf(stderr, "cannot decode %s\n", path);
			return 1;
		}
		const ma_uint32 sampleRate = decoder.outputSampleRate;
		const ma_result length = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
		ma_decoder_uninit(&decoder);
		if (length != MA_SUCCESS) {
			fprintf(stderr, "no length for %s\n", path);
			return 1;
		}

		char siteName[64];
		snprintf(siteName, sizeof(siteName), "%016" PRIx64 "%s", Hash(bytes, size), AudioExtension(sounds.items[i]));
		char sitePath[4096];
		snprintf(sitePath, sizeof(sitePath), "%s/%s", siteDirectory, siteName);
		struct stat existing;
		if (stat(sitePath, &existing) != 0 || (size_t)existing.st_size != size) {
			FILE* copy = fopen(sitePath, "wb");
			if (!copy || fwrite(bytes, 1, size, copy) != size) {
				fprintf(stderr, "cannot write %s\n", sitePath);
				return 1;
			}
			fclose(copy);
		}
		Append(&written, siteName);
		free(bytes);
		totalBytes += size;
		fprintf(manifest, "%s%s\t%zu\t%" PRIu64 "\t%u\t%s\n", prefix, sounds.items[i], size, (uint64_t)frames, sampleRate, siteName);
	}
	fclose(manifest);

	// Remove what earlier versions left behind.
	qsort(written.items, written.count, sizeof(char*), Compare);
	DIR* listing = opendir(siteDirectory);
	struct dirent* entry;
	size_t removed = 0;
	while (listing && (entry = readdir(listing))) {
		const char* name = entry->d_name;
		if (!AudioExtension(name) || bsearch(&name, written.items, written.count, sizeof(char*), Compare)) {
			continue;
		}
		char stale[4096];
		snprintf(stale, sizeof(stale), "%s/%s", siteDirectory, name);
		removed += remove(stale) == 0;
	}
	if (listing) {
		closedir(listing);
	}

	char finalPath[4096];
	snprintf(finalPath, sizeof(finalPath), "%s/manifest.tsv", siteDirectory);
	if (rename(manifestPath, finalPath) != 0) {
		fprintf(stderr, "cannot write %s\n", finalPath);
		return 1;
	}
	printf("audio manifest: %zu sounds, %.1f MB%s\n", sounds.count, totalBytes / 1048576.0, removed ? ", stale files removed" : "");
	return 0;
}
