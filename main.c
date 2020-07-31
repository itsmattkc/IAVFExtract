#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum ControlCodes {
  WAVE_DATA = 0x66,
  SMACKER_HEADER = 0x6A,
  SMACKER_FRAME = 0x6C,

  // Haven't implemented this yet, it appears to be a Smacker header but has a different IAVF
  // chunk header that I haven't figured out yet.
  SMACKER_HEADER_ALT = 0x78
};

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

void combine_smk(FILE** smk_head, FILE** smk_foot, FILE** smk_file,
                 const char* smk_header_filename, const char* smk_footer_filename)
{
  fseek(*smk_head, 0, SEEK_END);
  long header_sz = ftell(*smk_head);
  fseek(*smk_head, 0, SEEK_SET);

  fseek(*smk_foot, 0, SEEK_END);
  long footer_sz = ftell(*smk_foot);
  fseek(*smk_foot, 0, SEEK_SET);

  dump_data(*smk_head, *smk_file, header_sz);
  dump_data(*smk_foot, *smk_file, footer_sz);

  fclose(*smk_head);
  *smk_head = NULL;
  remove(smk_header_filename);

  fclose(*smk_foot);
  *smk_foot = NULL;
  remove(smk_footer_filename);

  fclose(*smk_file);
  *smk_file = NULL;
}

int extract_video(const char* filename, int verbose)
{
  // Set up default return value
  int ret = 1;

  // Create output files
  int smk_index = 1;
  FILE *smk_head = NULL, *smk_foot = NULL, *smk_file = NULL, *wav_file = NULL;

  // Variables
  uint16_t control_code;
  long file_pos;
  uint32_t wav_data_sz = 0;
  uint16_t video_width, video_height;
  uint16_t audio_freq;
  uint8_t audio_channels, audio_bit_depth;
  char wav_filename[1024];
  char smk_header_filename[1024];
  char smk_footer_filename[1024];
  char smk_file_filename[1024];

  // Attempt to open file
  FILE* file = fopen(filename, "rb");

  // If file failed to open in any way, return
  if (!file) {
    printf("Failed to open file \"%s\"\n", filename);
    goto end;
  }

  // Identify IAVF file
  {
    char identify_buffer[5];
    fread(identify_buffer, 4, 1, file);
    if (strcmp(identify_buffer, "IAVF") != 0) {
      printf("Failed to play video - file is not an IAVF video.\n");
      goto end;
    }

    printf("Extracting \"%s\"\n", filename);

    // Identify version
    fread(identify_buffer, 4, 1, file);
    printf("Format is IAVF %s\n", identify_buffer);
  }

  // Read video metadata
  fseek(file, 0x2F, SEEK_SET);
  fread(&video_height, sizeof(video_height), 1, file);
  fread(&video_width, sizeof(video_height), 1, file);

  printf("Video: %ix%i\n", video_width, video_height);

  // Read audio metadata
  fseek(file, 0x1C, SEEK_SET);

  fread(&audio_freq, sizeof(audio_freq), 1, file);
  fread(&audio_channels, sizeof(audio_channels), 1, file);
  fread(&audio_bit_depth, sizeof(audio_bit_depth), 1, file);

  printf("Audio: %i Hz, %i channel(s), %i-bit\n", audio_freq, audio_channels, audio_bit_depth);

  // Create WAV file
  sprintf(wav_filename, "%s.WAV", filename);
  if ((wav_file = fopen(wav_filename, "wb"))) {
    // Write WAV header
    write_wave_header(wav_file, audio_freq, audio_channels, audio_bit_depth);
  } else {
    printf("Failed to open output audio file. No audio will be extracted.\n");
    goto end;
  }

  // Seek to first chunk
  fseek(file, 0x91, SEEK_SET);

  while (fread(&control_code, sizeof(control_code), 1, file) > 0) {
    file_pos = ftell(file) - sizeof(control_code);

    switch (control_code) {
    case WAVE_DATA:
    {
      uint32_t index;
      uint32_t chunk_sz1;
      uint32_t chunk_sz2;

      fread(&index, sizeof(index), 1, file);
      fread(&chunk_sz1, sizeof(chunk_sz1), 1, file);
      fread(&chunk_sz2, sizeof(chunk_sz2), 1, file);

      if (verbose) {
        printf("Found WAV data at %lx. Index %i, Sz1: %i, Sz2: %i\n", file_pos, index, chunk_sz1, chunk_sz2);
      }

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
    case SMACKER_HEADER:
    {
      uint32_t smacker_chunk_sz;

      fread(&smacker_chunk_sz, sizeof(smacker_chunk_sz), 1, file);

      if (verbose) {
        printf("Found Smacker header at %lx of size %x\n", file_pos, smacker_chunk_sz);
      }

      if (control_code == SMACKER_HEADER) {
        fseek(file, 0x1C, SEEK_CUR);
      } else {

      }

      if (smk_file) {
        combine_smk(&smk_head, &smk_foot, &smk_file, smk_header_filename, smk_footer_filename);
      }

      // Generate filenames
      sprintf(smk_header_filename, "%s-%i.SMKHEAD", filename, smk_index);
      sprintf(smk_footer_filename, "%s-%i.SMKFOOT", filename, smk_index);
      sprintf(smk_file_filename, "%s-%i.SMK", filename, smk_index);
      smk_index++;

      // Open files
      smk_head = fopen(smk_header_filename, "wb+");
      smk_foot = fopen(smk_footer_filename, "wb+");
      smk_file = fopen(smk_file_filename, "wb");

      if (!smk_head || !smk_foot || !smk_file) {
        printf("Failed to open output Smacker file\n");
        goto end;
      }

      dump_data(file, smk_head, 0x68);

      dump_data(file, smk_foot, smacker_chunk_sz - 0x68);
      break;
    }
    case SMACKER_FRAME:
    {
      uint32_t chunk_sz;
      fread(&chunk_sz, sizeof(chunk_sz), 1, file);

      if (verbose) {
        printf("Found Smacker frame at %lx of size %x\n", file_pos, chunk_sz);
      }

      fseek(file, 0x8, SEEK_CUR);

      fwrite(&chunk_sz, sizeof(chunk_sz), 1, smk_head);

      dump_data(file, smk_foot, chunk_sz);
      break;
    }
    case 0x67:
    case 0x68:
    case 0x70:
    case 0x75:
    case 0x77:
    case 0x79:
      if (verbose) {
        printf("Found %x at %lx. Uncertain behavior, trying skipping 0xC bytes ahead.\n", control_code, file_pos);
      }
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

  // Combine SMKs
  if (smk_file && smk_head && smk_foot) {
    combine_smk(&smk_head, &smk_foot, &smk_file, smk_header_filename, smk_footer_filename);
  }

  // Close SMK
  if (smk_file) {
    fclose(smk_file);
  }

  // Close SMK header
  if (smk_head) {
    fclose(smk_head);
  }

  // Close SMK footer
  if (smk_foot) {
    fclose(smk_foot);
  }

  // Close input file
  if (file) {
    fclose(file);
  }

  // Return value will be 1 (failure) unless we reach the end of the loop successfully
  return ret;
}

void print_help(const char* bin_name)
{
  // Print default help
  printf("IAVFExtract 0.0.1\n"
         "Copyright (C) 2020 MattKC\n"
         "Usage: %s [--verbose] <path-to-file>\n"
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

  int verbose = 0;
  for (int i=1; i<argc; i++) {
    if (!stricmp(argv[i], "-v") || !stricmp(argv[i], "--verbose")) {
      verbose = 1;
    }
  }

  // Otherwise, try to play the video
  return extract_video(argv[1], verbose);
}
