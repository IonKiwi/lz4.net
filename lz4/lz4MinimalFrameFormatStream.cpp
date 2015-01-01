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

#include "lz4MinimalFrameFormatStream.h"
#include "lz4.h"
#include "lz4hc.h"

#define KB *(1 <<10)

namespace lz4 {

	LZ4MinimalFrameFormatStream::LZ4MinimalFrameFormatStream(Stream^ innerStream, CompressionMode compressionMode, bool leaveInnerStreamOpen) {

		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }

		_innerStream = innerStream;
		_compressionMode = compressionMode;
		_leaveInnerStreamOpen = leaveInnerStreamOpen;
		_blockSize = 64 KB;
		_ringbufferSlots = 2; // created linked blocks (up to LZ4 window size, which is 64 KB)
		InitRingbuffer();
	}

	LZ4MinimalFrameFormatStream::LZ4MinimalFrameFormatStream(Stream^ innerStream, CompressionMode compressionMode, int blockSize, bool leaveInnerStreamOpen) {

		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }
		else if (blockSize < 1) {
			throw gcnew ArgumentOutOfRangeException("blockSize");
		}

		_innerStream = innerStream;
		_compressionMode = compressionMode;
		_leaveInnerStreamOpen = leaveInnerStreamOpen;
		_blockSize = blockSize;

		int ringbufferSlots = 2; // created linked blocks (up to LZ4 window size, which is 64 KB)
		if (blockSize < 64 KB) {
			ringbufferSlots = (int)Math::Ceiling(2 * 64 KB / blockSize);
		}
		_ringbufferSlots = ringbufferSlots;
		InitRingbuffer();
	}

	LZ4MinimalFrameFormatStream::LZ4MinimalFrameFormatStream(Stream^ innerStream, CompressionMode compressionMode, int blockSize, int ringbufferSlots, bool leaveInnerStreamOpen) {

		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }
		else if (blockSize < 1) {
			throw gcnew ArgumentOutOfRangeException("blockSize");
		}
		else if (ringbufferSlots < 1) {
			throw gcnew ArgumentOutOfRangeException("ringbufferSlots");
		}

		_innerStream = innerStream;
		_compressionMode = compressionMode;
		_leaveInnerStreamOpen = leaveInnerStreamOpen;
		_blockSize = blockSize;
		_ringbufferSlots = ringbufferSlots;
		InitRingbuffer();
	}

	LZ4MinimalFrameFormatStream::~LZ4MinimalFrameFormatStream() {
		Flush();
		if (!_leaveInnerStreamOpen) {
			delete _innerStream;
		}
		this->!LZ4MinimalFrameFormatStream();
	}

	LZ4MinimalFrameFormatStream::!LZ4MinimalFrameFormatStream() {
		if (_lz4DecodeStream != nullptr) { LZ4_freeStreamDecode(_lz4DecodeStream); _lz4DecodeStream = nullptr; }
		if (_lz4Stream != nullptr) { LZ4_freeStream(_lz4Stream); _lz4Stream = nullptr; }
		delete[] _ringbuffer; _ringbuffer = nullptr;
	}

	void LZ4MinimalFrameFormatStream::InitRingbuffer() {
		_ringbufferSize = _ringbufferSlots * _blockSize; // +_blockSize;
		_ringbuffer = new char[_ringbufferSize];
		if (_ringbuffer == nullptr) {
			throw gcnew OutOfMemoryException();
		}

		if (_compressionMode == CompressionMode::Compress) {
			_lz4Stream = LZ4_createStream();
		}
		else {
			_lz4DecodeStream = LZ4_createStreamDecode();
		}
	}

	void LZ4MinimalFrameFormatStream::FlushCurrentChunk() {
		if (_inputBufferOffset <= 0) { return; }

		char *inputPtr = &_ringbuffer[_ringbufferOffset];

		array<byte>^ outputBuffer = gcnew array<byte>(LZ4_COMPRESSBOUND(_inputBufferOffset));
		pin_ptr<byte> outputBufferPtr = &outputBuffer[0];

		if (_ringbufferSlots == 1) {
			// reset the stream { create independently compressed blocks }
			LZ4_loadDict(_lz4Stream, nullptr, 0);
		}
		int outputBytes = LZ4_compress_continue(_lz4Stream, inputPtr, (char *)outputBufferPtr, _inputBufferOffset);
		if (outputBytes <= 0) { throw gcnew Exception("Compress failed"); }

		array<byte>^ b = gcnew array<byte>(4);
		b[0] = (byte)((unsigned int)outputBytes & 0xFF);
		b[1] = (byte)(((unsigned int)outputBytes >> 8) & 0xFF);
		b[2] = (byte)(((unsigned int)outputBytes >> 16) & 0xFF);
		b[3] = (byte)(((unsigned int)outputBytes >> 24) & 0xFF);

		_innerStream->Write(b, 0, b->Length);
		_innerStream->Write(outputBuffer, 0, outputBytes);

		// update ringbuffer offset
		_ringbufferOffset += _inputBufferOffset;
		// wraparound the ringbuffer offset
		if (_ringbufferOffset > _ringbufferSize - _blockSize) _ringbufferOffset = 0;
		// reset input offset
		_inputBufferOffset = 0;
	}

	bool LZ4MinimalFrameFormatStream::AcquireNextChunk() {

		// read chunk size
		array<byte>^ sizeBuffer = gcnew array<byte>(4);
		int bytesRead = _innerStream->Read(sizeBuffer, 0, sizeBuffer->Length);
		if (bytesRead == 0) { return false; }
		else if (bytesRead != sizeBuffer->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }
		else if ((sizeBuffer[sizeBuffer->Length - 1] & 0x80) == 0x80) { throw gcnew Exception("Invalid data"); }

		int sizeValue = 0;
		for (int i = sizeBuffer->Length - 1; i >= 0; i--) {
			sizeValue |= ((int)sizeBuffer[i] << (i * 8));
		}

		if (sizeValue == 0) {
			return false;
		}

		// update ringbuffer offset
		_ringbufferOffset += _inputBufferLength;
		// wraparound the ringbuffer offset
		if (_ringbufferOffset > _ringbufferSize - _blockSize) _ringbufferOffset = 0;

		// read chunk data
		array<byte>^ chunkData = gcnew array<byte>(sizeValue);
		bytesRead = _innerStream->Read(chunkData, 0, sizeValue);
		if (bytesRead != chunkData->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		pin_ptr<byte> inputBufferPtr = &chunkData[0];
		char *outputPtr = &_ringbuffer[_ringbufferOffset];
		int outputBytes = LZ4_decompress_safe_continue(_lz4DecodeStream, (char *)inputBufferPtr, outputPtr, chunkData->Length, _blockSize);
		if (outputBytes <= 0) { throw gcnew Exception("Decompress failed"); }

		// set the input offset
		_inputBufferLength = outputBytes;
		_inputBufferOffset = 0;
		return true;
	}

	bool LZ4MinimalFrameFormatStream::Get_CanRead() {
		return _compressionMode == CompressionMode::Decompress;
	}

	bool LZ4MinimalFrameFormatStream::Get_CanSeek() {
		return false;
	}

	bool LZ4MinimalFrameFormatStream::Get_CanWrite() {
		return _compressionMode == CompressionMode::Compress;
	}

	long long LZ4MinimalFrameFormatStream::Get_Length() {
		return -1;
	}

	long long LZ4MinimalFrameFormatStream::Get_Position() {
		return -1;
	}

	void LZ4MinimalFrameFormatStream::Flush() {
		if (_inputBufferOffset > 0 && CanWrite) { FlushCurrentChunk(); };
	}

	int LZ4MinimalFrameFormatStream::ReadByte() {
		if (!CanRead) { throw gcnew NotSupportedException("Read"); }

		if (_inputBufferOffset >= _inputBufferLength && !AcquireNextChunk())
			return -1; // that's just end of stream
		return _ringbuffer[_ringbufferOffset + _inputBufferOffset++];
	}

	int LZ4MinimalFrameFormatStream::Read(array<byte>^ buffer, int offset, int count) {
		if (!CanRead) { throw gcnew NotSupportedException("Read"); }

		int total = 0;
		while (count > 0)
		{
			int chunk = Math::Min(count, _inputBufferLength - _inputBufferOffset);
			if (chunk > 0)
			{
				using System::Runtime::InteropServices::Marshal;

				char *inputPtr = &_ringbuffer[_ringbufferOffset + _inputBufferOffset];
				Marshal::Copy(IntPtr(inputPtr), buffer, offset, chunk);

				_inputBufferOffset += chunk;
				offset += chunk;
				count -= chunk;
				total += chunk;
			}
			else
			{
				if (!AcquireNextChunk()) break;
			}
		}
		return total;
	}

	long long LZ4MinimalFrameFormatStream::Seek(long long offset, SeekOrigin origin) {
		throw gcnew NotSupportedException("Seek");
	}

	void LZ4MinimalFrameFormatStream::SetLength(long long value) {
		throw gcnew NotSupportedException("SetLength");
	}

	void LZ4MinimalFrameFormatStream::WriteByte(byte value) {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }

		if (_inputBufferOffset >= _blockSize)
		{
			FlushCurrentChunk();
		}

		_ringbuffer[_ringbufferOffset + _inputBufferOffset++] = value;
	}

	void LZ4MinimalFrameFormatStream::Write(array<byte>^ buffer, int offset, int count) {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }

		while (count > 0)
		{
			int chunk = Math::Min(count, _blockSize - _inputBufferOffset);
			if (chunk > 0)
			{
				using System::Runtime::InteropServices::Marshal;

				// write data to ringbuffer
				// NOTE: we could also pin buffer and do a memcpy
				char *inputPtr = &_ringbuffer[_ringbufferOffset + _inputBufferOffset];
				Marshal::Copy(buffer, offset, IntPtr(inputPtr), chunk);

				offset += chunk;
				count -= chunk;
				_inputBufferOffset += chunk;
			}
			else
			{
				FlushCurrentChunk();
			}
		}
	}
}
