
#define _GNU_SOURCE // getline
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <arpa/inet.h> // htonl
#include <stdlib.h> // getline
#include <string.h> // getline

#include "util.h"

const uint8_t PACKAGE_MAGIC[4] = { 'L', 'i', 'z', '1' };

int pack() {
	char * name = NULL;
	size_t namesize = 0;

	uint8_t header[32];
	memcpy(header, PACKAGE_MAGIC, 4);

	ssize_t namelen;
	while ((namelen = getline(&name, &namesize, stdin)) >= 0) {
		namelen = trimnl(name, namelen);
		if (namelen == 0) {
			continue;
		}

		struct stat st;
		if (stat(name, &st) < 0) {
			fprintf(stderr, "Warning: could not stat %s (%s)\n", name, strerror(errno));
			continue;
		}
		size_t filesize = st.st_size;

		if (!S_ISREG(st.st_mode)) {
			fprintf(stderr, "Warning: can't open %s - not a regular file\n", name);
			continue;
		}

		FILE * f = fopen(name, "rb");
		if (!f) {
			fprintf(stderr, "Warning: can't fopen %s for read (%s)\n", name, strerror(errno));
			continue;
		}

		uint8_t * hdrend = header + 4;
		varuint_pack(&hdrend, filesize);
		varuint_pack(&hdrend, st.st_mtime);

		uint8_t miscflags = 0;
		if (st.st_mode & S_IXUSR) {
			miscflags |= 1;
		}

		*hdrend = miscflags;
		hdrend++;

		uint32_t crc = crc32mpeg(0xFFFFFFFF, header, hdrend - header);
		if (!fwrite(header, hdrend - header, 1, stdout)) {
			fprintf(stderr, "Fatal error: unexpected end-of-stream when writing Lizarchive header (%s)\n", strerror(errno));
			return 1;
		}

		crc = crc32mpeg(crc, name, namelen + 1); // +1 for null terminator
		if (!fwrite(name, namelen + 1, 1, stdout)) {
			fprintf(stderr, "Fatal error: unexpected end-of-stream when writing file name (%s)\n", strerror(errno));
			return 1;
		}

		while (filesize) {
			uint8_t buf[8192];
			size_t read = fread(buf, 1, min(filesize, sizeof(buf)), f);
			if (read == 0) {
				fprintf(stderr, "Fatal error: unexpected end-of-stream when reading file data from %s (%s)\n", name, strerror(errno));
				fclose(f);
				return 1;
			}

			if (!fwrite(buf, read, 1, stdout)) {
				fprintf(stderr, "Fatal error: unexpected end-of-stream when writing file data (%s)\n", strerror(errno));
				fclose(f);
				return 1;
			}

			crc = crc32mpeg(crc, buf, read);
			filesize -= read;
		}
		fclose(f);

		crc = htonl(crc);
		if (!fwrite(&crc, 4, 1, stdout)) {
			fprintf(stderr, "Fatal error: unexpected end-of-stream when writing CRC (%s)\n", strerror(errno));
			return 1;
		}
	}

	return 0;
}

int unpack() {
	char * name = NULL;
	size_t namesize = 0;

	while (1) {
		uint8_t header[32];

		switch (fread(header, 1, 4, stdin)) {
			default: // Got garbage, warn and leave
				fprintf(stderr, "Warning: trailing garbage\n");

			case 0: // OK, end of stream
				return 0;

			case 4: // OK, read fully
				break;
		}

		if (memcmp(PACKAGE_MAGIC, header, 4) != 0) {
			fprintf(stderr, "Fatal error: expected magic signature %02X%02X%02X%02X, got %02X%02X%02X%02X\n", PACKAGE_MAGIC[0], PACKAGE_MAGIC[1], PACKAGE_MAGIC[2], PACKAGE_MAGIC[3], header[0], header[1], header[2], header[3]);
			return 1;
		}

		uint8_t * hdrend = header + 4;
		if (
			!varuint_read(&hdrend, header + sizeof(header) - 1, stdin) ||
			!varuint_read(&hdrend, header + sizeof(header) - 1, stdin) ||
			!fread(hdrend, 1, 1, stdin)
		) {
			fprintf(stderr, "Fatal error: could not read header information\n");
			return 1;
		}

		uint64_t filesize;
		uint64_t modtime;
		bool executable;
		hdrend = header + 4;
		varuint_unpack(&hdrend, header + sizeof(header) - 1, &filesize);
		varuint_unpack(&hdrend, header + sizeof(header) - 1, &modtime);
		executable = (*hdrend & 0x01 ? true : false);
		hdrend++;

		uint32_t crc = crc32mpeg(0xFFFFFFFF, header, hdrend - header);

		ssize_t namelen = getdelim(&name, &namesize, '\0', stdin);
		if (namelen < 0) {
			fprintf(stderr, "Fatal error: could not real file name (%s)\n", strerror(errno));
			return 1;
		}
		crc = crc32mpeg(crc, name, namelen); // Note NO +1 because getdelim includes delimiter in string length

		FILE * f = fopen(name, "wb");
		if (!f) {
			fprintf(stderr, "Warning: can't fopen %s for write. Skipping (%s)\n", name, strerror(errno));
			if (fseek(stdin, filesize + 4, SEEK_CUR)) {
				fprintf(stderr, "Fatal error: can't skip payload (%s)\n", strerror(errno));
				return 1;
			}
			continue;
		}

		while (filesize) {
			uint8_t buf[8192];
			size_t read = fread(buf, 1, min(filesize, sizeof(buf)), stdin);
			if (read == 0) {
				fprintf(stderr, "Fatal error: unexpected end-of-stream when reading file data (%s)\n", strerror(errno));
				fclose(f);
				return 1;
			}

			if (!fwrite(buf, read, 1, f)) {
				fprintf(stderr, "Fatal error: unexpected end-of-stream when writing file data to %s (%s)\n", name, strerror(errno));
				fclose(f);
				return 1;
			}

			crc = crc32mpeg(crc, buf, read);
			filesize -= read;
		}
		fclose(f);
		// TODO: set executable bit

		uint32_t readcrc;
		if (!fread(&readcrc, sizeof(readcrc), 1, stdin)) {
			fprintf(stderr, "Fatal error: can't read CRC from stdin (%s)\n", strerror(errno));
			return 1;
		}
		readcrc = ntohl(readcrc);
		if (readcrc != crc) {
			fprintf(stderr, "Warning: CRC mismatch for %s: calculated %08X, should be %08X\n", name, crc, readcrc);
		}
	}
}

int main(int argc, char ** argv) {
	if (argc == 2) {
		if (strcmp("pack", argv[1]) == 0) {
			return pack();
		}

		if (strcmp("unpack", argv[1]) == 0) {
			return unpack();
		}
	}

	fprintf(stderr, "Usage: %s (pack|unpack)\n", argv[0]);
	return 1;
}
