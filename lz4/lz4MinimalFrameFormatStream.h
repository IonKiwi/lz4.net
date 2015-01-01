/*
   Header File
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

#pragma once

#include "lz4.h"

using namespace System;
using namespace System::IO;
using namespace System::IO::Compression;

namespace lz4 {

	public ref class LZ4MinimalFrameFormatStream : Stream
	{
	private:
		typedef unsigned char byte;
		int _blockSize;
		int _ringbufferSlots;
		int _ringbufferSize;
		char *_ringbuffer = nullptr;
		int _ringbufferOffset = 0;
		LZ4_stream_t *_lz4Stream = nullptr;
		LZ4_streamDecode_t *_lz4DecodeStream = nullptr;
		bool _leaveInnerStreamOpen;
		//array<byte>^ _tmpBuffer = nullptr;
		//int _tmpBufferSize = 0;
		
		//char *_outputBuffer = nullptr;
		//array<byte>^ _inputBuffer;
		//array<byte>^ _outputBuffer;
		
		int _inputBufferOffset = 0;
		int _inputBufferLength = 0;
		Stream^ _innerStream;
		CompressionMode _compressionMode;

		bool Get_CanRead();
		bool Get_CanSeek();
		bool Get_CanWrite();
		long long Get_Length();
		long long Get_Position();
		
		bool AcquireNextChunk();
		void FlushCurrentChunk();
		
		void InitRingbuffer();
	public:
		LZ4MinimalFrameFormatStream(Stream^ innerStream, CompressionMode compressionMode, bool leaveInnerStreamOpen);
		LZ4MinimalFrameFormatStream(Stream^ innerStream, CompressionMode compressionMode, int blockSize, bool leaveInnerStreamOpen);
		LZ4MinimalFrameFormatStream(Stream^ innerStream, CompressionMode compressionMode, int blockSize, int ringbufferSlots, bool leaveInnerStreamOpen);
		~LZ4MinimalFrameFormatStream();
		!LZ4MinimalFrameFormatStream();

		property virtual bool CanRead {
			bool get() override {
				return Get_CanRead();
			};
		};
		property virtual bool CanSeek {
			bool get() override {
				return Get_CanSeek();
			}
		}
		property virtual bool CanWrite {
			bool get() override {
				return Get_CanWrite();
			}
		}
		property virtual long long Length {
			long long get() override {
				return Get_Length();
			}
		}
		property virtual long long Position {
			long long get() override {
				return Get_Position();
			}
			void set(long long value) override {
				throw gcnew NotSupportedException("SetPosition");
			}
		}
		virtual void Flush() override;
		virtual int ReadByte() override;
		virtual int Read(array<byte>^ buffer, int offset, int count) override;
		virtual long long Seek(long long offset, SeekOrigin origin) override;
		virtual void SetLength(long long value) override;
		virtual void WriteByte(byte value) override;
		virtual void Write(array<byte>^ buffer, int offset, int count) override;
	};
}
