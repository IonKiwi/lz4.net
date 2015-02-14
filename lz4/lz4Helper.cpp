#include "stdafx.h"
/*
   Source File
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   source repository: https://github.com/IonKiwi/lz4.net
   */

#include "lz4Helper.h"
#include "lz4.h"
#include "lz4hc.h"

namespace lz4 {
	array<Byte>^ LZ4Helper::Custom::Compress(array<Byte>^ input, int inputOffset, int inputLength, int passes)
	{
		if (input == nullptr) {
			throw gcnew ArgumentNullException("input");
		}
		else if (inputOffset < 0) {
			throw gcnew ArgumentOutOfRangeException("inputOffset");
		}
		else if (inputLength <= 0) {
			throw gcnew ArgumentOutOfRangeException("inputLength");
		}
		else if (inputOffset + inputLength > input->Length) {
			throw gcnew ArgumentOutOfRangeException("inputOffset+inputLength");
		}
		else if (passes < 1 || passes > 2) {
			throw gcnew ArgumentOutOfRangeException("passes");
		}

		int bufferSize = LZ4_compressBound(inputLength);
		int offset = 0;
		array<Byte>^ result = gcnew array<Byte>(bufferSize + 10);
		if (passes == 1) {
			result[0] = 1; // 1 pass
			offset = inputLength;
			int i;
			for (i = 0; offset && i < 8; i++) {
				result[2 + i] = (offset & 0xff);
				offset >>= 8;
			}
			if (offset != 0) {
				throw gcnew NotSupportedException("input too large");
			}
			result[1] = i;
			offset = 2 + i;
		}

		pin_ptr<Byte> inputPtr = &input[inputOffset];
		byte* inputBytePtr = inputPtr;
		pin_ptr<Byte> outputPtr = &result[offset];
		byte* outputBytePtr = outputPtr;

		int compressedSize = LZ4_compress((char*)inputBytePtr, (char*)outputBytePtr, inputLength);
		if (compressedSize <= 0)
		{
			throw gcnew Exception("Compression failed");
		}

		if (passes == 1) {
			array<Byte>^ slimResult = gcnew array<Byte>(compressedSize + offset);
			Buffer::BlockCopy(result, 0, slimResult, 0, compressedSize + offset);
			return slimResult;
		}

		int bufferSize2 = LZ4_compressBound(compressedSize);
		array<Byte>^ result2 = gcnew array<Byte>(bufferSize2 + 10);
		result2[0] = 2; // 2 pass
		offset = inputLength;
		int i;
		for (i = 0; offset && i < 8; i++) {
			result2[2 + i] = (offset & 0xff);
			offset >>= 8;
		}
		if (offset != 0) {
			throw gcnew NotSupportedException("input too large");
		}

		result2[1] = i;
		offset = 2 + i;

		inputPtr = &result[0];
		inputBytePtr = inputPtr;
		outputPtr = &result2[offset];
		outputBytePtr = outputPtr;

		int compressedSize2 = LZ4_compressHC((char *)inputBytePtr, (char *)outputBytePtr, compressedSize);

		array<Byte>^ slimResult = gcnew array<Byte>(compressedSize2 + offset);
		Buffer::BlockCopy(result2, 0, slimResult, 0, compressedSize2 + offset);
		return slimResult;
	}

	array<Byte>^ LZ4Helper::Custom::Decompress(array<Byte>^ input, int inputOffset, int inputLength)
	{
		if (input == nullptr) {
			throw gcnew ArgumentNullException("input");
		}
		else if (inputOffset < 0) {
			throw gcnew ArgumentOutOfRangeException("inputOffset");
		}
		else if (inputLength < 3) {
			throw gcnew ArgumentOutOfRangeException("inputLength");
		}
		else if (inputOffset + inputLength > input->Length) {
			throw gcnew ArgumentOutOfRangeException("inputOffset+inputLength");
		}

		int passes = input[0];
		int sizeOfSize = input[1];
		if (sizeOfSize > 8 || inputLength < (2 + (int)sizeOfSize) || !(passes == 1 || passes == 2)) {
			throw gcnew Exception("Invalid data");
		}

		int fileSize = 0;
		for (int i = 0; i < sizeOfSize; i++) {
			fileSize += (((byte)input[2 + i]) << (8 * i));
		}

		if (fileSize < 0)
		{
			throw gcnew Exception("Invalid data");
		}

		int bufferSize1 = fileSize;
		if (passes == 2) {
			bufferSize1 = LZ4_compressBound(fileSize);
		}

		array<Byte>^ result = gcnew array<Byte>(bufferSize1);

		pin_ptr<Byte> inputPtr = &input[2 + sizeOfSize];
		byte* inputBytePtr = inputPtr;
		pin_ptr<Byte> outputPtr = &result[0];
		byte* outputBytePtr = outputPtr;

		int bufferSize2 = LZ4_decompress_safe((char *)inputBytePtr, (char *)outputBytePtr, inputLength - 2 - sizeOfSize, bufferSize1);
		if (bufferSize2 <= 0)
		{
			throw gcnew Exception("Decompression failed");
		}

		if (passes == 1)
		{
			array<Byte>^ slimResult = gcnew array<Byte>(bufferSize2);
			Buffer::BlockCopy(result, 0, slimResult, 0, bufferSize2);
			return slimResult;
		}

		array<Byte>^ result2 = gcnew array<Byte>(fileSize);

		inputPtr = &result[0];
		inputBytePtr = inputPtr;
		outputPtr = &result2[0];
		outputBytePtr = outputPtr;

		int bufferSize3 = LZ4_decompress_safe((char *)inputBytePtr, (char *)outputBytePtr, bufferSize2, fileSize);
		if (bufferSize3 <= 0)
		{
			throw gcnew Exception("Decompression failed");
		}

		array<Byte>^ slimResult = gcnew array<Byte>(bufferSize3);
		Buffer::BlockCopy(result2, 0, slimResult, 0, bufferSize3);
		return slimResult;
	}

	array<Byte>^ LZ4Helper::Frame::Compress(array<Byte>^ input, int inputOffset, int inputLength, LZ4FrameBlockMode blockMode, LZ4FrameBlockSize blockSize, LZ4FrameChecksumMode checksumMode, long long maxFrameSize)
	{
		if (input == nullptr) {
			throw gcnew ArgumentNullException("input");
		}
		else if (inputOffset < 0) {
			throw gcnew ArgumentOutOfRangeException("inputOffset");
		}
		else if (inputLength <= 0) {
			throw gcnew ArgumentOutOfRangeException("inputLength");
		}
		else if (inputOffset + inputLength > input->Length) {
			throw gcnew ArgumentOutOfRangeException("inputOffset+inputLength");
		}

		MemoryStream^ ms = nullptr;
		array<Byte>^ result;
		try
		{
			ms = gcnew MemoryStream();
			LZ4Stream^ lz4 = nullptr;
			try
			{
				lz4 = LZ4Stream::CreateCompressor(ms, blockMode, blockSize, checksumMode, maxFrameSize, true);
				lz4->Write(input, inputOffset, inputLength);
			}
			finally
			{
				if (lz4 != nullptr) { delete lz4; }
			}
			result = ms->ToArray();
		}
		finally
		{
			if (ms != nullptr) { delete ms; }
		}

		return result;
	}

	array<Byte>^ LZ4Helper::Frame::Decompress(array<Byte>^ input, int inputOffset, int inputLength)
	{
		if (input == nullptr) {
			throw gcnew ArgumentNullException("input");
		}
		else if (inputOffset < 0) {
			throw gcnew ArgumentOutOfRangeException("inputOffset");
		}
		else if (inputLength < 3) {
			throw gcnew ArgumentOutOfRangeException("inputLength");
		}
		else if (inputOffset + inputLength > input->Length) {
			throw gcnew ArgumentOutOfRangeException("inputOffset+inputLength");
		}

		MemoryStream ^ms = nullptr, ^ms2 = nullptr;
		array<Byte>^ result;
		try
		{
			ms = gcnew MemoryStream(input, inputOffset, inputLength);
			ms2 = gcnew MemoryStream();
			LZ4Stream^ lz4 = nullptr;
			try
			{
				lz4 = LZ4Stream::CreateDecompressor(ms, true);
				lz4->CopyTo(ms2);
			}
			finally
			{
				if (lz4 != nullptr) { delete lz4; }
			}
			result = ms2->ToArray();
		}
		finally
		{
			if (ms != nullptr) { delete ms; }
			if (ms2 != nullptr) { delete ms2; }
		}

		return result;
	}
}
