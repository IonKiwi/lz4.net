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

#include "lz4FramingStream.h"
#include "lz4.h"
#include "xxhash.h"

#define KB *(1 <<10)
#define MB *(1 <<20)

typedef unsigned int        U32;

namespace lz4 {

	LZ4FramingStream::LZ4FramingStream() {

	}

	LZ4FramingStream^ LZ4FramingStream::CreateCompressor(Stream^ innerStream, LZ4FrameBlockMode blockMode, LZ4FrameBlockSize blockSize, LZ4FrameChecksumMode checksumMode, long long maxFrameSize, bool leaveInnerStreamOpen) {
		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }

		LZ4FramingStream^ result = gcnew LZ4FramingStream();
		result->_innerStream = innerStream;
		result->_compressionMode = CompressionMode::Compress;
		result->_checksumMode = checksumMode;
		result->_blockMode = blockMode;
		result->_blockSize = blockSize;
		result->_maxFrameSize = maxFrameSize;
		result->_leaveInnerStreamOpen = leaveInnerStreamOpen;
		result->Init();

		return result;
	}

	LZ4FramingStream^ LZ4FramingStream::CreateDecompressor(Stream^ innerStream, bool leaveInnerStreamOpen) {
		if (innerStream == nullptr) { throw gcnew ArgumentNullException("innerStream"); }

		LZ4FramingStream^ result = gcnew LZ4FramingStream();
		result->_innerStream = innerStream;
		result->_compressionMode = CompressionMode::Decompress;
		result->_leaveInnerStreamOpen = leaveInnerStreamOpen;
		result->Init();

		return result;
	}

	LZ4FramingStream::~LZ4FramingStream() {
		if (CanWrite) { WriteEndFrame(); }
		if (!_leaveInnerStreamOpen) {
			delete _innerStream;
		}
		this->!LZ4FramingStream();
	}

	LZ4FramingStream::!LZ4FramingStream() {
		if (_lz4Stream != nullptr) { LZ4_freeStream(_lz4Stream); _lz4Stream = nullptr; }
		if (_contentHashState != nullptr) { delete _contentHashState; _contentHashState = nullptr; }
		if (_lz4DecodeStream != nullptr) { LZ4_freeStreamDecode(_lz4DecodeStream); _lz4DecodeStream = nullptr; }
	}

	void LZ4FramingStream::Init() {
		if (_compressionMode == CompressionMode::Compress) {
			_lz4Stream = LZ4_createStream();

			switch (_blockSize) {
			case LZ4FrameBlockSize::Max64KB:
				_inputBufferSize = 64 KB;
				_outputBufferSize = 64 KB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				break;
			case LZ4FrameBlockSize::Max256KB:
				_inputBufferSize = 256 KB;
				_outputBufferSize = 256 KB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				break;
			case LZ4FrameBlockSize::Max1MB:
				_inputBufferSize = 1 MB;
				_outputBufferSize = 1 MB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				break;
			case LZ4FrameBlockSize::Max4MB:
				_inputBufferSize = 4 MB;
				_outputBufferSize = 4 MB;
				_inputBuffer = gcnew array<byte>(2 * _inputBufferSize);
				_outputBuffer = gcnew array<byte>(_outputBufferSize);
				break;
			default:
				throw gcnew NotSupportedException(_blockSize.ToString());
			}

			if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
				_contentHashState = XXH32_createState();
				XXH32_reset(_contentHashState, 0);
			}
		}
		else {
			_lz4DecodeStream = LZ4_createStreamDecode();
			_contentHashState = XXH32_createState();
			XXH32_reset(_contentHashState, 0);
		}
	}

	bool LZ4FramingStream::Get_CanRead() {
		return _compressionMode == CompressionMode::Decompress;
	}

	bool LZ4FramingStream::Get_CanSeek() {
		return false;
	}

	bool LZ4FramingStream::Get_CanWrite() {
		return _compressionMode == CompressionMode::Compress;
	}

	long long LZ4FramingStream::Get_Length() {
		return -1;
	}

	long long LZ4FramingStream::Get_Position() {
		return -1;
	}

	long long LZ4FramingStream::Seek(long long offset, SeekOrigin origin) {
		throw gcnew NotSupportedException("Seek");
	}

	void LZ4FramingStream::SetLength(long long value) {
		throw gcnew NotSupportedException("SetLength");
	}

	void LZ4FramingStream::Flush() {
		if (_inputBufferOffset > 0 && CanWrite) { FlushCurrentBlock(false); }
	}

	void LZ4FramingStream::WriteEndFrame() {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }

		if (!_hasWrittenInitialStartFrame) {
			//WriteEmptyFrame();
			return;
		}

		if (!_hasWrittenStartFrame) {
			return;
			//throw gcnew InvalidOperationException("No start frame was written");
		}

		if (_inputBufferOffset > 0) {
			// flush current block first
			FlushCurrentBlock(true);
		}

		array<byte>^ b = gcnew array<byte>(4);

		// write end mark
		b[0] = 0;
		b[1] = 0;
		b[2] = 0;
		b[3] = 0; // 0x80
		_innerStream->Write(b, 0, b->Length);

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			U32 xxh = XXH32_digest(_contentHashState);
			XXH32_reset(_contentHashState, 0); // reset for next frame

			b[0] = (byte)(xxh & 0xFF);
			b[1] = (byte)((xxh >> 8) & 0xFF);
			b[2] = (byte)((xxh >> 16) & 0xFF);
			b[3] = (byte)((xxh >> 24) & 0xFF);
			_innerStream->Write(b, 0, b->Length);
		}

		// reset the stream
		LZ4_loadDict(_lz4Stream, nullptr, 0);

		_hasWrittenStartFrame = false;
	}

	void LZ4FramingStream::WriteStartFrame() {
		_hasWrittenStartFrame = true;
		_hasWrittenInitialStartFrame = true;
		_frameCount++;
		_blockCount = 0;
		//_ringbufferOffset = 0;

		// write magic
		array<byte>^ magic = gcnew array<byte>(4);
		magic[0] = 0x04;
		magic[1] = 0x22;
		magic[2] = 0x4D;
		magic[3] = 0x18;
		_innerStream->Write(magic, 0, magic->Length);

		// frame descriptor
		array<byte>^ descriptor = gcnew array<byte>(2); // if _storeContentSize +8
		descriptor[0] = 0;
		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			descriptor[0] |= 0x4;
		}

		//if (_storeContentSize) {
		//	descriptor[0] |= 0x8;
		//}

		if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
			descriptor[0] |= 0x10;
		}

		if (_blockMode == LZ4FrameBlockMode::Independent) {
			descriptor[0] |= 0x20;
		}

		descriptor[0] |= 0x40; // version 01

		descriptor[1] = 0;
		if (_blockSize == LZ4FrameBlockSize::Max64KB) {
			descriptor[1] |= (4 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max256KB) {
			descriptor[1] |= (5 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max1MB) {
			descriptor[1] |= (6 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max4MB) {
			descriptor[1] |= (7 << 4);
		}
		else {
			throw gcnew NotImplementedException();
		}

		//if (_storeContentSize) {
		//	unsigned long long contentsize = 0; // we cant support this, as the contentsize is not known in advance
		//	descriptor[2] = (byte)(contentsize & 0xFF);
		//	descriptor[3] = (byte)((contentsize >> 8) & 0xFF);
		//	descriptor[4] = (byte)((contentsize >> 16) & 0xFF);
		//	descriptor[5] = (byte)((contentsize >> 24) & 0xFF);
		//	descriptor[6] = (byte)((contentsize >> 32) & 0xFF);
		//	descriptor[7] = (byte)((contentsize >> 40) & 0xFF);
		//	descriptor[8] = (byte)((contentsize >> 48) & 0xFF);
		//	descriptor[9] = (byte)((contentsize >> 56) & 0xFF);
		//}

		pin_ptr<byte> descriptorPtr = &descriptor[0];
		U32 xxh = XXH32(descriptorPtr, descriptor->Length, 0);

		_innerStream->Write(descriptor, 0, descriptor->Length);
		_innerStream->WriteByte((xxh >> 8) & 0xFF);
	}

	void LZ4FramingStream::WriteEmptyFrame() {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }

		if (_hasWrittenStartFrame || _hasWrittenInitialStartFrame) {
			throw gcnew InvalidOperationException("should not have happend, hasWrittenStartFrame: " + _hasWrittenStartFrame + ", hasWrittenInitialStartFrame: " + _hasWrittenInitialStartFrame);
		}

		// write magic
		array<byte>^ magic = gcnew array<byte>(4);
		magic[0] = 0x04;
		magic[1] = 0x22;
		magic[2] = 0x4D;
		magic[3] = 0x18;
		_innerStream->Write(magic, 0, magic->Length);

		// frame descriptor
		array<byte>^ descriptor = gcnew array<byte>(2);
		descriptor[0] = 0;
		if (_blockMode == LZ4FrameBlockMode::Independent) { descriptor[0] |= 0x20; }
		descriptor[0] |= 0x40; // version 01

		descriptor[1] = 0;
		if (_blockSize == LZ4FrameBlockSize::Max64KB) {
			descriptor[1] |= (4 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max256KB) {
			descriptor[1] |= (5 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max1MB) {
			descriptor[1] |= (6 << 4);
		}
		else if (_blockSize == LZ4FrameBlockSize::Max4MB) {
			descriptor[1] |= (7 << 4);
		}
		else {
			throw gcnew NotImplementedException();
		}

		pin_ptr<byte> descriptorPtr = &descriptor[0];
		U32 xxh = XXH32(descriptorPtr, descriptor->Length, 0);

		_innerStream->Write(descriptor, 0, descriptor->Length);
		_innerStream->WriteByte((xxh >> 8) & 0xFF);

		array<byte>^ endMarker = gcnew array<byte>(4);
		_innerStream->Write(endMarker, 0, endMarker->Length);

		_hasWrittenInitialStartFrame = true;
		_frameCount++;
	}

	void LZ4FramingStream::WriteUserDataFrame(int id, array<byte>^ buffer, int offset, int count) {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }

		if (id < 0 || id > 15) { throw gcnew ArgumentOutOfRangeException("id"); }
		else if (buffer == nullptr) { throw gcnew ArgumentNullException("buffer"); }
		else if (count < 0) { throw gcnew ArgumentOutOfRangeException("count"); }
		else if (offset < 0) { throw gcnew ArgumentOutOfRangeException("offset"); }
		else if (offset + count > buffer->Length) { throw gcnew ArgumentOutOfRangeException("offset + count"); }

		if (!_hasWrittenInitialStartFrame) {
			// write empty frame according to spec recommendation
			WriteEmptyFrame();
		}

		// write magic
		array<byte>^ magic = gcnew array<byte>(4);
		magic[0] = (0x50 + id);
		magic[1] = 0x2A;
		magic[2] = 0x4D;
		magic[3] = 0x18;
		_innerStream->Write(magic, 0, magic->Length);

		// write size
		array<byte>^ b = gcnew array<byte>(4);
		b[0] = (byte)((unsigned int)count & 0xFF);
		b[1] = (byte)(((unsigned int)count >> 8) & 0xFF);
		b[2] = (byte)(((unsigned int)count >> 16) & 0xFF);
		b[3] = (byte)(((unsigned int)count >> 24) & 0xFF);
		_innerStream->Write(b, 0, b->Length);

		// write data
		_innerStream->Write(buffer, offset, count);
	}

	void LZ4FramingStream::FlushCurrentBlock(bool suppressEndFrame) {

		pin_ptr<byte> inputBufferPtr = &_inputBuffer[_ringbufferOffset];
		pin_ptr<byte> outputBufferPtr = &_outputBuffer[0];

		if (!_hasWrittenStartFrame) {
			WriteStartFrame();
		}

		if (_blockMode == LZ4FrameBlockMode::Independent) {
			// reset the stream { create independently compressed blocks }
			LZ4_loadDict(_lz4Stream, nullptr, 0);
		}

		int targetSize;
		bool isCompressed;

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			XXH_errorcode status = XXH32_update(_contentHashState, inputBufferPtr, _inputBufferOffset);
			if (status != XXH_errorcode::XXH_OK) {
				throw gcnew Exception("Failed to update content checksum");
			}
		}

		int outputBytes = LZ4_compress_limitedOutput_continue(_lz4Stream, (char *)inputBufferPtr, (char *)outputBufferPtr, _inputBufferOffset, _outputBufferSize);
		if (outputBytes == 0) {
			// compression failed or output is too large

			// reset the stream [is this necessary??]
			LZ4_loadDict(_lz4Stream, nullptr, 0);

			Buffer::BlockCopy(_inputBuffer, _ringbufferOffset, _outputBuffer, 0, _inputBufferOffset);
			targetSize = _inputBufferOffset;
			isCompressed = false;
		}
		else if (outputBytes < 0) {
			throw gcnew Exception("Compress failed");
		}
		else {
			targetSize = outputBytes;
			isCompressed = true;
		}

		array<byte>^ b = gcnew array<byte>(4);
		b[0] = (byte)((unsigned int)targetSize & 0xFF);
		b[1] = (byte)(((unsigned int)targetSize >> 8) & 0xFF);
		b[2] = (byte)(((unsigned int)targetSize >> 16) & 0xFF);
		b[3] = (byte)(((unsigned int)targetSize >> 24) & 0xFF);

		if (!isCompressed) {
			b[3] |= 0x80;
		}

		_innerStream->Write(b, 0, b->Length);
		_innerStream->Write(_outputBuffer, 0, targetSize);

		if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
			pin_ptr<byte> targetPtr = &_outputBuffer[0];
			U32 xxh = XXH32(targetPtr, targetSize, 0);

			b[0] = (byte)(xxh & 0xFF);
			b[1] = (byte)((xxh >> 8) & 0xFF);
			b[2] = (byte)((xxh >> 16) & 0xFF);
			b[3] = (byte)((xxh >> 24) & 0xFF);
			_innerStream->Write(b, 0, b->Length);
		}

		_inputBufferOffset = 0; // reset before calling WriteEndFrame() !!
		_blockCount++;

		if (_maxFrameSize > 0 && _blockCount >= _maxFrameSize) {
			WriteEndFrame();
		}

		// update ringbuffer offset
		_ringbufferOffset += _inputBufferSize;
		// wraparound the ringbuffer offset
		if (_ringbufferOffset > _inputBufferSize) _ringbufferOffset = 0;
	}

	bool LZ4FramingStream::GetFrameInfo() {

		if (_hasFrameInfo) {
			throw gcnew Exception("should not have happend, _hasFrameInfo: " + _hasFrameInfo);
		}

		array<byte>^ magic = gcnew array<byte>(4);
		int bytesRead = _innerStream->Read(magic, 0, magic->Length);
		if (bytesRead == 0) {
			return false;
		}
		else if (bytesRead != magic->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		_blockCount = 0;
		if (magic[0] == 0x04 && magic[1] == 0x22 && magic[2] == 0x4D && magic[3] == 0x18) {
			// lz4 frame
			_frameCount++;

			// reset state
			XXH32_reset(_contentHashState, 0);
			_blockSize = LZ4FrameBlockSize::Max64KB;
			_blockMode = LZ4FrameBlockMode::Linked;
			_checksumMode = LZ4FrameChecksumMode::None;
			_contentSize = 0;
			_outputBufferOffset = 0;
			_outputBufferBlockSize = 0;
			//_ringbufferOffset = 0;

			// read frame descriptor
			array<byte>^ descriptor = gcnew array<byte>(2);
			bytesRead = _innerStream->Read(descriptor, 0, descriptor->Length);
			if (bytesRead != descriptor->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			// verify version
			if ((descriptor[0] & 0x40) != 0x40 || (descriptor[0] & 0x80) != 0x00) {
				throw gcnew Exception("Unexpected frame version");
			}
			else if ((descriptor[0] & 0x01) != 0x00) {
				throw gcnew Exception("Predefined dictionaries are not supported");
			}
			else if ((descriptor[0] & 0x02) != 0x00) {
				// reserved value
				throw gcnew Exception("Header contains unexpected value");
			}

			if ((descriptor[0] & 0x04) == 0x04) {
				_checksumMode = _checksumMode | LZ4FrameChecksumMode::Content;
			}
			bool hasContentSize = false;
			if ((descriptor[0] & 0x08) == 0x08) {
				hasContentSize = true;
			}
			if ((descriptor[0] & 0x10) == 0x10) {
				_checksumMode = _checksumMode | LZ4FrameChecksumMode::Block;
			}
			if ((descriptor[0] & 0x20) == 0x20) {
				_blockMode = LZ4FrameBlockMode::Independent;
			}

			if ((descriptor[1] & 0x0F) != 0x00) {
				// reserved value
				throw gcnew Exception("Header contains unexpected value");
			}
			else if ((descriptor[1] & 0x80) != 0x00) {
				// reserved value
				throw gcnew Exception("Header contains unexpected value");
			}

			int blockSizeId = (descriptor[1] & 0x70) >> 4;
			if (blockSizeId == 4) {
				_blockSize = LZ4FrameBlockSize::Max64KB;
				_inputBufferSize = 64 KB;
				_outputBufferSize = 64 KB;
			}
			else if (blockSizeId == 5) {
				_blockSize = LZ4FrameBlockSize::Max256KB;
				_inputBufferSize = 256 KB;
				_outputBufferSize = 256 KB;
			}
			else if (blockSizeId == 6) {
				_blockSize = LZ4FrameBlockSize::Max1MB;
				_inputBufferSize = 1 MB;
				_outputBufferSize = 1 MB;
			}
			else if (blockSizeId == 7) {
				_blockSize = LZ4FrameBlockSize::Max4MB;
				_inputBufferSize = 4 MB;
				_outputBufferSize = 4 MB;
			}
			else {
				throw gcnew Exception("Unsupported block size: " + blockSizeId);
			}

			if (hasContentSize) {
				byte tmp1 = descriptor[0], tmp2 = descriptor[1];
				descriptor = gcnew array<byte>(2 + 8);
				descriptor[0] = tmp1;
				descriptor[1] = tmp2;
				bytesRead = _innerStream->Read(descriptor, 2, 8);
				if (bytesRead != 8) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

				for (int i = 9; i >= 2; i--) {
					_contentSize |= ((unsigned long long)descriptor[i] << (i * 8));
				}
			}

			// read checksum
			array<byte>^ checksum = gcnew array<byte>(1);
			bytesRead = _innerStream->Read(checksum, 0, checksum->Length);
			if (bytesRead != checksum->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }
			// verify checksum
			pin_ptr<byte> descriptorPtr = &descriptor[0];
			U32 xxh = XXH32(descriptorPtr, descriptor->Length, 0);
			byte checksumByte = (xxh >> 8) & 0xFF;
			if (checksum[0] != checksumByte) {
				throw gcnew Exception("Frame checksum is invalid");
			}

			// resize buffers
			_inputBuffer = gcnew array<byte>(_inputBufferSize);
			_outputBuffer = gcnew array<byte>(2 * _outputBufferSize);

			_hasFrameInfo = true;
			return true;
		}
		else if (magic[0] >= 0x50 && magic[0] <= 0x5f && magic[1] == 0x2A && magic[2] == 0x4D && magic[3] == 0x18) {
			// skippable frame
			_frameCount++;

			// read frame size
			array<byte>^ b = gcnew array<byte>(4);
			bytesRead = _innerStream->Read(b, 0, b->Length);
			if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			unsigned int frameSize = 0;
			for (int i = b->Length - 1; i >= 0; i--) {
				frameSize |= ((unsigned int)b[i] << (i * 8));
			}

			array<byte>^ userData = gcnew array<byte>(frameSize);
			bytesRead = _innerStream->Read(userData, 0, userData->Length);
			if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			int id = (magic[0] & 0xF);

			LZ4UserDataFrameEventArgs^ e = gcnew LZ4UserDataFrameEventArgs(id, userData);
			UserDataFrameRead(this, e);

			// read next frame header
			return GetFrameInfo();
		}
		else {
			throw gcnew Exception("lz4 stream is corrupt");
		}
	}

	bool LZ4FramingStream::AcquireNextBlock() {
		if (!_hasFrameInfo) {
			if (!GetFrameInfo()) {
				return false;
			}
		}

		array<byte>^ b = gcnew array<byte>(4);

		// read block size
		int bytesRead = _innerStream->Read(b, 0, b->Length);
		if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		bool isCompressed = true;
		if ((b[3] & 0x80) == 0x80) {
			isCompressed = false;
			b[3] &= 0x7F;
		}

		unsigned int blockSize = 0;
		for (int i = b->Length - 1; i >= 0; i--) {
			blockSize |= ((unsigned int)b[i] << (i * 8));
		}

		if (blockSize > (unsigned int)_outputBufferSize) {
			throw gcnew Exception("Block size exceeds maximum block size");
		}

		if (blockSize == 0) {
			// end marker

			//_outputBufferSize = 0;
			//_outputBufferOffset = 0;
			//_outputBufferBlockSize = 0;
			//_inputBufferSize = 0;
			//_inputBufferOffset = 0;
			_hasFrameInfo = false;

			if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
				// calculate hash
				U32 xxh = XXH32_digest(_contentHashState);

				// read hash
				b = gcnew array<byte>(4);
				bytesRead = _innerStream->Read(b, 0, b->Length);
				if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

				if (b[0] != (byte)(xxh & 0xFF) ||
					b[1] != (byte)((xxh >> 8) & 0xFF) ||
					b[2] != (byte)((xxh >> 16) & 0xFF) ||
					b[3] != (byte)((xxh >> 24) & 0xFF)) {
					throw gcnew Exception("Content checksum did not match");
				}
			}

			return AcquireNextBlock();
		}

		// read block data
		bytesRead = _innerStream->Read(_inputBuffer, 0, blockSize);
		if (bytesRead != blockSize) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

		_blockCount++;

		if ((_checksumMode & LZ4FrameChecksumMode::Block) == LZ4FrameChecksumMode::Block) {
			// read block checksum
			b = gcnew array<byte>(4);
			bytesRead = _innerStream->Read(b, 0, b->Length);
			if (bytesRead != b->Length) { throw gcnew EndOfStreamException("Unexpected end of stream"); }

			unsigned int checksum = 0;
			for (int i = b->Length - 1; i >= 0; i--) {
				checksum |= ((unsigned int)b[i] << (i * 8));
			}
			// verify checksum
			pin_ptr<byte> targetPtr = &_inputBuffer[0];
			U32 xxh = XXH32(targetPtr, blockSize, 0);
			if (checksum != xxh) {
				throw gcnew Exception("Block checksum did not match");
			}
		}

		// update ringbuffer offset
		_ringbufferOffset += _outputBufferSize;
		// wraparound the ringbuffer offset
		if (_ringbufferOffset > _outputBufferSize) _ringbufferOffset = 0;

		if (!isCompressed) {
			Buffer::BlockCopy(_inputBuffer, 0, _outputBuffer, _ringbufferOffset, blockSize);
			_outputBufferBlockSize = blockSize;
		}
		else {
			pin_ptr<byte> inputPtr = &_inputBuffer[0];
			pin_ptr<byte> outputPtr = &_outputBuffer[_ringbufferOffset];
			int decompressedSize = LZ4_decompress_safe_continue(_lz4DecodeStream, (char *)inputPtr, (char *)outputPtr, blockSize, _outputBufferSize);
			if (decompressedSize <= 0) {
				throw gcnew Exception("Decompress failed");
			}
			_outputBufferBlockSize = decompressedSize;
		}

		if ((_checksumMode & LZ4FrameChecksumMode::Content) == LZ4FrameChecksumMode::Content) {
			pin_ptr<byte> contentPtr = &_outputBuffer[_ringbufferOffset];
			XXH_errorcode status = XXH32_update(_contentHashState, contentPtr, _outputBufferBlockSize);
			if (status != XXH_errorcode::XXH_OK) {
				throw gcnew Exception("Failed to update content checksum");
			}
		}

		_outputBufferOffset = 0;

		return true;
	}

	int LZ4FramingStream::ReadByte() {
		if (!CanRead) { throw gcnew NotSupportedException("Read"); }

		if (_outputBufferOffset >= _outputBufferBlockSize && !AcquireNextBlock())
			return -1; // that's just end of stream
		return _outputBuffer[_ringbufferOffset + _outputBufferOffset++];
	}

	int LZ4FramingStream::Read(array<byte>^ buffer, int offset, int count) {
		if (!CanRead) { throw gcnew NotSupportedException("Read"); }
		else if (buffer == nullptr) { throw gcnew ArgumentNullException("buffer"); }
		else if (offset < 0) { throw gcnew ArgumentOutOfRangeException("offset"); }
		else if (count < 0) { throw gcnew ArgumentOutOfRangeException("count"); }
		else if (offset + count > buffer->Length) { throw gcnew ArgumentOutOfRangeException("offset+count"); }

		int total = 0;
		while (count > 0)
		{
			int chunk = Math::Min(count, _outputBufferBlockSize - _outputBufferOffset);
			if (chunk > 0)
			{
				Buffer::BlockCopy(_outputBuffer, _ringbufferOffset + _outputBufferOffset, buffer, offset, chunk);

				_outputBufferOffset += chunk;
				offset += chunk;
				count -= chunk;
				total += chunk;
			}
			else
			{
				if (!AcquireNextBlock()) break;
			}
		}
		return total;
	}

	void LZ4FramingStream::WriteByte(byte value) {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }

		if (!_hasWrittenStartFrame) { WriteStartFrame(); }

		if (_inputBufferOffset >= _inputBufferSize)
		{
			FlushCurrentBlock(false);
		}

		_inputBuffer[_ringbufferOffset + _inputBufferOffset++] = value;
	}

	void LZ4FramingStream::Write(array<byte>^ buffer, int offset, int count) {
		if (!CanWrite) { throw gcnew NotSupportedException("Write"); }
		else if (buffer == nullptr) { throw gcnew ArgumentNullException("buffer"); }
		else if (offset < 0) { throw gcnew ArgumentOutOfRangeException("offset"); }
		else if (count < 0) { throw gcnew ArgumentOutOfRangeException("count"); }
		else if (offset + count > buffer->Length) { throw gcnew ArgumentOutOfRangeException("offset+count"); }

		if (count == 0) { return; }

		if (!_hasWrittenStartFrame) { WriteStartFrame(); }

		while (count > 0)
		{
			int chunk = Math::Min(count, _inputBufferSize - _inputBufferOffset);
			if (chunk > 0)
			{
				Buffer::BlockCopy(buffer, offset, _inputBuffer, _ringbufferOffset + _inputBufferOffset, chunk);

				offset += chunk;
				count -= chunk;
				_inputBufferOffset += chunk;
			}
			else
			{
				FlushCurrentBlock(false);
			}
		}
	}
}
