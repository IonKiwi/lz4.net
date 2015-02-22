using lz4.AnyCPU.loader;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace lz4 {
	public static class LZ4Helper {

		//public static class Custom {

		//	public static byte[] Compress(byte[] input) {
		//		return LZ4Loader.Compress1()(input);
		//	}

		//	public static byte[] Compress(byte[] input, int inputOffset, int inputLength, int passes) {
		//		return LZ4Loader.Compress2()(input, inputOffset, inputLength, passes);
		//	}

		//	public static byte[] Decompress(byte[] input) {
		//		return LZ4Loader.Decompress1()(input);
		//	}

		//	public static byte[] Decompress(byte[] input, int inputOffset, int inputLength) {
		//		return LZ4Loader.Decompress2()(input, inputOffset, inputLength);
		//	}
		//}

		//public static class Frame {

		public static byte[] Compress(byte[] input, LZ4FrameBlockMode blockMode = LZ4FrameBlockMode.Linked, LZ4FrameBlockSize blockSize = LZ4FrameBlockSize.Max64KB, LZ4FrameChecksumMode checksumMode = LZ4FrameChecksumMode.Content, long? maxFrameSize = null) {
			return LZ4Loader.Compress3()(input, blockMode, blockSize, checksumMode, maxFrameSize);
		}

		public static byte[] Compress(byte[] input, int inputOffset, int inputLength, LZ4FrameBlockMode blockMode = LZ4FrameBlockMode.Linked, LZ4FrameBlockSize blockSize = LZ4FrameBlockSize.Max64KB, LZ4FrameChecksumMode checksumMode = LZ4FrameChecksumMode.Content, long? maxFrameSize = null) {
			return LZ4Loader.Compress4()(input, inputOffset, inputLength, blockMode, blockSize, checksumMode, maxFrameSize);
		}

		public static byte[] Decompress(byte[] input) {
			return LZ4Loader.Decompress3()(input);
		}

		public static byte[] Decompress(byte[] input, int inputOffset, int inputLength) {
			return LZ4Loader.Decompress4()(input, inputOffset, inputLength);
		}

		//}
	}
}
