#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void write_wave_header(FILE* file, int freq, int channels, int bitdepth)
{
  fwrite("RIFF", 4, 1, file);

  // WAV filesize (fill in later)
  uint32_t temp = 0;
  fwrite(&temp, 4, 1, file);

  fwrite("WAVE", 4, 1, file);
  fwrite("fmt ", 4, 1, file);

  temp = 16;
  fwrite(&temp, 4, 1, file);

  temp = 1;
  fwrite(&temp, 2, 1, file);

  fwrite(&channels, 2, 1, file);

  fwrite(&freq, 4, 1, file);

  temp = (freq * bitdepth * channels) / 8;
  fwrite(&temp, 4, 1, file);

  temp = (bitdepth * channels) / 8;
  fwrite(&temp, 2, 1, file);

  fwrite(&bitdepth, 2, 1, file);

  fwrite("data", 4, 1, file);

  temp = 0;
  fwrite(&temp, 4, 1, file);
}

void write_wave_footer(FILE* file, uint32_t data_sz)
{
  fseek(file, 40, SEEK_SET);
  fwrite(&data_sz, 4, 1, file);

  data_sz += 36;
  fseek(file, 4, SEEK_SET);
  fwrite(&data_sz, 4, 1, file);
}

void dump_data(FILE* in, FILE* out, uint32_t len)
{
  void* data = malloc(len);
  fread(data, len, 1, in);
  fwrite(data, len, 1, out);
  free(data);
}

int play_video(const char* filename)
{
  // Attempt to open file
  FILE* file = fopen(filename, "rb");

  // If file failed to open in any way, return
  if (!file) {
    printf("Failed to open file \"%s\"\n", filename);
    return 1;
  }

  // Identify IAVF file
  char identify_buffer[5];
  fread(identify_buffer, 4, 1, file);
  if (strcmp(identify_buffer, "IAVF") != 0) {
    printf("Failed to play video - file is not an IAVF video.\n");
    return 1;
  }

  printf("Extracting \"%s\"\n", filename);

  // Identify version
  fread(identify_buffer, 4, 1, file);
  printf("Format is IAVF %s\n", identify_buffer);

  // Set up default return value
  int ret = 1;

  // Read video metadata
  fseek(file, 0x2F, SEEK_SET);
  uint16_t video_width;
  uint16_t video_height;
  fread(&video_height, sizeof(video_height), 1, file);
  fread(&video_width, sizeof(video_height), 1, file);

  printf("Video: %ix%i\n", video_width, video_height);

  // Create SMK file
  char smk_filename[1024];
  sprintf(smk_filename, "%s.smk", filename);
  FILE* smk_file = fopen(smk_filename, "wb");

  // Handle SMK failure
  if (!smk_file) {
    printf("Failed to open output video file \"%s\". No video will be extracted.\n", smk_filename);
  }

  // Read audio metadata
  fseek(file, 0x1C, SEEK_SET);
  uint16_t freq;
  uint8_t channels;
  uint8_t bit_depth;

  fread(&freq, sizeof(freq), 1, file);
  fread(&channels, sizeof(channels), 1, file);
  fread(&bit_depth, sizeof(bit_depth), 1, file);

  printf("Audio: %i Hz, %i channel(s), %i-bit\n", freq, channels, bit_depth);

  // Create WAV file
  char wav_filename[1024];
  sprintf(wav_filename, "%s.wav", filename);
  FILE* wav_file = fopen(wav_filename, "wb");

  // Handle failure to open WAV output
  if (wav_file) {
    // Write WAV header
    write_wave_header(wav_file, freq, channels, bit_depth);
  } else {
    printf("Failed to open output audio file \"%s\". No audio will be extracted.\n", wav_filename);
  }

  // Seek to first chunk
  fseek(file, 0x91, SEEK_SET);

  uint16_t control_code;
  long file_pos;
  uint32_t wav_data_sz = 0;

  while (fread(&control_code, sizeof(control_code), 1, file) > 0) {
    file_pos = ftell(file) - sizeof(control_code);

    switch (control_code) {
    case 0x66:
    {
      uint32_t index;
      uint32_t chunk_sz1;
      uint32_t chunk_sz2;

      fread(&index, sizeof(index), 1, file);
      fread(&chunk_sz1, sizeof(chunk_sz1), 1, file);
      fread(&chunk_sz2, sizeof(chunk_sz2), 1, file);

      printf("Found WAV data at %lx. Index %i, Sz1: %i, Sz2: %i\n", file_pos, index, chunk_sz1, chunk_sz2);

      if (chunk_sz2 > 0) {
        if (wav_file) {
          wav_data_sz += chunk_sz2;

          dump_data(file, wav_file, chunk_sz2);
        } else {
          fseek(file, chunk_sz2, SEEK_CUR);
        }
      }
      break;
    }
    case 0x6A:
    {
      uint32_t smacker_chunk_sz;

      fread(&smacker_chunk_sz, sizeof(smacker_chunk_sz), 1, file);

      printf("Found Smacker data at %lx of size %x\n", file_pos, smacker_chunk_sz);

      fseek(file, 0x1C, SEEK_CUR);

      if (smk_file) {
        dump_data(file, smk_file, 0x68);
      } else {
        fseek(file, 0x68, SEEK_CUR);
      }

      fseek(file, smacker_chunk_sz - 0x68, SEEK_CUR);
      break;
    }
    case 0x6C:
    {
      uint32_t chunk_sz;
      fread(&chunk_sz, sizeof(chunk_sz), 1, file);

      printf("Found Smacker SECONDARY data %lx of size %x\n", file_pos, chunk_sz);

      fseek(file, 0x8, SEEK_CUR);

      if (smk_file) {
        dump_data(file, smk_file, chunk_sz);
      } else {
        fseek(file, chunk_sz, SEEK_CUR);
      }
      break;
    }
    case 0x67:
    case 0x68:
    case 0x70:
    case 0x75:
    case 0x77:
    case 0x79:
      printf("Found %x at %lx. Uncertain behavior, trying skipping 0xC bytes ahead.\n", control_code, file_pos);
      fseek(file, 0xC, SEEK_CUR);
      break;
    default:
      printf("Found unknown opcode %x at %lx. Stopping.\n", control_code, file_pos);

      // Exit switch statement and loop
      goto end;
    }
  }

  // Set return value to success
  ret = 0;

end:
  // Close WAV file
  if (wav_file) {
    write_wave_footer(wav_file, wav_data_sz);
    fclose(wav_file);
  }

  // Close SMK file
  if (smk_file) {
    fclose(smk_file);
  }

  // Close input file
  fclose(file);

  // Return value will be 1 (failure) unless we reach the end of the loop successfully
  return ret;
}

void print_help(const char* bin_name)
{
  // Print default help
  printf("IAVFExtract 0.0.1\n"
         "Copyright (C) 2020 MattKC\n"
         "Usage: %s <path-to-file>\n"
         "Extracts video and audio streams from Take-Two Interactive's proprietary "
         "IAVF video format.\n"
         "\n", bin_name);
}

int main(int argc, char* argv[])
{
  // Default action if no arguments are passed
  if (argc < 2) {
    print_help(argv[0]);
    return 1;
  }

  // Otherwise, try to play the video
  return play_video(argv[1]);
}
