using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Linq.Expressions;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace lz4.AnyCPU.loader {
	internal static class LZ4Loader {

		private static bool _initialized = false;

		public static void Ensure() {
			if (!_initialized) {
				lock (typeof(LZ4Loader)) {
					if (!_initialized) {
						InitializeInternal();
						_initialized = true;
					}
				}
			}
		}

		private static void InitializeInternal() {
			var asm = LoadLZ4Assembly();
			if (asm == null) { throw new InvalidOperationException("Failed to load lz4 assembly"); }

			var helperType1 = asm.GetType("lz4.LZ4Helper+Custom", true);
			var helperType2 = asm.GetType("lz4.LZ4Helper+Frame", true);
			var streamType = asm.GetType("lz4.LZ4Stream", true);
			var eventArgsType = asm.GetType("lz4.LZ4UserDataFrameEventArgs", true);

			var streamModeType = asm.GetType("lz4.LZ4StreamMode", true);
			var blockModeType = asm.GetType("lz4.LZ4FrameBlockMode", true);
			var blockSizeType = asm.GetType("lz4.LZ4FrameBlockSize", true);
			var checksumType = asm.GetType("lz4.LZ4FrameChecksumMode", true);

			var c1 = helperType1.GetMethod("Compress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]) }, null);
			var c1p1 = Expression.Parameter(typeof(byte[]));
			var c1ce = Expression.Call(c1, c1p1);
			_compress1 = Expression.Lambda<Func<byte[], byte[]>>(c1ce, c1p1).Compile();

			var c2 = helperType1.GetMethod("Compress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]), typeof(int), typeof(int), typeof(int) }, null);
			var c2p1 = Expression.Parameter(typeof(byte[]));
			var c2p2 = Expression.Parameter(typeof(int));
			var c2p3 = Expression.Parameter(typeof(int));
			var c2p4 = Expression.Parameter(typeof(int));
			var c2ce = Expression.Call(c2, c2p1, c2p2, c2p3, c2p4);
			_compress2 = Expression.Lambda<Func<byte[], int, int, int, byte[]>>(c2ce, c2p1, c2p2, c2p3, c2p4).Compile();

			var d1 = helperType1.GetMethod("Decompress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]) }, null);
			var d1p1 = Expression.Parameter(typeof(byte[]));
			var d1ce = Expression.Call(d1, d1p1);
			_decompress1 = Expression.Lambda<Func<byte[], byte[]>>(d1ce, d1p1).Compile();

			var d2 = helperType1.GetMethod("Decompress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]), typeof(int), typeof(int) }, null);
			var d2p1 = Expression.Parameter(typeof(byte[]));
			var d2p2 = Expression.Parameter(typeof(int));
			var d2p3 = Expression.Parameter(typeof(int));
			var d2ce = Expression.Call(d2, d2p1, d2p2, d2p3);
			_decompress2 = Expression.Lambda<Func<byte[], int, int, byte[]>>(d2ce, d2p1, d2p2, d2p3).Compile();

			var cc1 = streamType.GetMethod("CreateCompressor", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(Stream), streamModeType, blockModeType, blockSizeType, checksumType, typeof(long?), typeof(bool) }, null);
			var cc1p1 = Expression.Parameter(typeof(Stream));
			var cc1p2 = Expression.Parameter(typeof(LZ4StreamMode));
			var cc1p3 = Expression.Parameter(typeof(LZ4FrameBlockMode));
			var cc1p4 = Expression.Parameter(typeof(LZ4FrameBlockSize));
			var cc1p5 = Expression.Parameter(typeof(LZ4FrameChecksumMode));
			var cc1p6 = Expression.Parameter(typeof(long?));
			var cc1p7 = Expression.Parameter(typeof(bool));
			var cc1p2_c = Expression.Convert(cc1p2, streamModeType);
			var cc1p3_c = Expression.Convert(cc1p3, blockModeType);
			var cc1p4_c = Expression.Convert(cc1p4, blockSizeType);
			var cc1p5_c = Expression.Convert(cc1p5, checksumType);
			var cc1ce = Expression.Call(cc1, cc1p1, cc1p2_c, cc1p3_c, cc1p4_c, cc1p5_c, cc1p6, cc1p7);
			_createCompressor = Expression.Lambda<Func<Stream, LZ4StreamMode, LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, bool, Stream>>(cc1ce, cc1p1, cc1p2, cc1p3, cc1p4, cc1p5, cc1p6, cc1p7).Compile();

			var cd1 = streamType.GetMethod("CreateDecompressor", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(Stream), streamModeType, typeof(bool) }, null);
			var cd1p1 = Expression.Parameter(typeof(Stream));
			var cd1p2 = Expression.Parameter(typeof(LZ4StreamMode));
			var cd1p3 = Expression.Parameter(typeof(bool));
			var cd1p2_c = Expression.Convert(cd1p2, streamModeType);
			var cd1ce = Expression.Call(cd1, cd1p1, cd1p2_c, cd1p3);
			_createDecompressor = Expression.Lambda<Func<Stream, LZ4StreamMode, bool, Stream>>(cd1ce, cd1p1, cd1p2, cd1p3).Compile();

			var cb = streamType.GetProperty("CurrentBlockCount", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
			var cbi = Expression.Parameter(typeof(Stream));
			var cbi_c = Expression.Convert(cbi, streamType);
			var cbm = cb.GetGetMethod(true);
			var cbmce = Expression.Call(cbi_c, cbm);
			_currentBlockCount = Expression.Lambda<Func<Stream, long>>(cbmce, cbi).Compile();

			var fc = streamType.GetProperty("FrameCount", BindingFlags.Public | BindingFlags.Instance);
			var fci = Expression.Parameter(typeof(Stream));
			var fci_c = Expression.Convert(fci, streamType);
			var fcm = fc.GetGetMethod(false);
			var fcmce = Expression.Call(fci_c, fcm);
			_frameCount = Expression.Lambda<Func<Stream, long>>(fcmce, fci).Compile();

			var ufe = streamType.GetEvent("UserDataFrameRead", BindingFlags.Public | BindingFlags.Instance);
			var ufei = Expression.Parameter(typeof(Stream));
			var efei_c = Expression.Convert(ufei, streamType);
			var efep1 = Expression.Parameter(typeof(Action<object, LZ4UserDataFrameEventArgs>));

			var eaHandlerP1 = Expression.Parameter(typeof(object));
			var eaHandlerP2 = Expression.Parameter(eventArgsType);

			var eaIdProperty = eventArgsType.GetProperty("Id", BindingFlags.Public | BindingFlags.Instance);
			var eaIdMethod = eaIdProperty.GetGetMethod(false);
			var eaIdCallExpression = Expression.Call(eaHandlerP2, eaIdMethod);
			var eaDataProperty = eventArgsType.GetProperty("Data", BindingFlags.Public | BindingFlags.Instance);
			var eaDataMethod = eaDataProperty.GetGetMethod(false);
			var eaDataCallExpression = Expression.Call(eaHandlerP2, eaDataMethod);
			var ea = typeof(LZ4UserDataFrameEventArgs).GetConstructor(BindingFlags.Public | BindingFlags.Instance, null, new Type[] { typeof(int), typeof(byte[]) }, null);
			var newEA = Expression.New(ea, eaIdCallExpression, eaDataCallExpression);
			var callHandler = Expression.Invoke(efep1, eaHandlerP1, newEA);

			//Type x = typeof(Action<,>).MakeGenericType(typeof(object), eventArgsType);
			Type x = typeof(EventHandler<>).MakeGenericType(eventArgsType);
			var handler = Expression.Lambda(x, callHandler, eaHandlerP1, eaHandlerP2);

			var ufem = ufe.GetAddMethod(false);
			var efemce = Expression.Call(efei_c, ufem, handler);
			_userFrameEvent = Expression.Lambda<Action<Stream, Action<object, LZ4UserDataFrameEventArgs>>>(efemce, ufei, efep1).Compile();

			var endFrameMethod = streamType.GetMethod("WriteEndFrame", BindingFlags.Public | BindingFlags.Instance);
			var endFrameP1 = Expression.Parameter(typeof(Stream));
			var endFrameP1Converted = Expression.Convert(endFrameP1, streamType);
			var endFrameCallExpression = Expression.Call(endFrameP1Converted, endFrameMethod);
			_writeEndFrame = Expression.Lambda<Action<Stream>>(endFrameCallExpression, endFrameP1).Compile();

			var userFrameMethod = streamType.GetMethod("WriteUserDataFrame", BindingFlags.Public | BindingFlags.Instance);
			var userFrameP1 = Expression.Parameter(typeof(Stream));
			var userFrameP2 = Expression.Parameter(typeof(int));
			var userFrameP3 = Expression.Parameter(typeof(byte[]));
			var userFrameP4 = Expression.Parameter(typeof(int));
			var userFrameP5 = Expression.Parameter(typeof(int));
			var userFrameP1Converted = Expression.Convert(userFrameP1, streamType);
			var userFrameCallExpression = Expression.Call(userFrameP1Converted, userFrameMethod, userFrameP2, userFrameP3, userFrameP4, userFrameP5);
			_writeUserDataFrame = Expression.Lambda<Action<Stream, int, byte[], int, int>>(userFrameCallExpression, userFrameP1, userFrameP2, userFrameP3, userFrameP4, userFrameP5).Compile();


			var c3 = helperType2.GetMethod("Compress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]), blockModeType, blockSizeType, checksumType, typeof(long?) }, null);
			var c3p1 = Expression.Parameter(typeof(byte[]));
			var c3p2 = Expression.Parameter(typeof(LZ4FrameBlockMode));
			var c3p2_c = Expression.Convert(c3p2, blockModeType);
			var c3p3 = Expression.Parameter(typeof(LZ4FrameBlockSize));
			var c3p3_c = Expression.Convert(c3p3, blockSizeType);
			var c3p4 = Expression.Parameter(typeof(LZ4FrameChecksumMode));
			var c3p4_c = Expression.Convert(c3p4, checksumType);
			var c3p5 = Expression.Parameter(typeof(long?));
			var c3ce = Expression.Call(c3, c3p1, c3p2_c, c3p3_c, c3p4_c, c3p5);
			_compress3 = Expression.Lambda<Func<byte[], LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, byte[]>>(c3ce, c3p1, c3p2, c3p3, c3p4, c3p5).Compile();

			var c4 = helperType2.GetMethod("Compress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]), typeof(int), typeof(int), blockModeType, blockSizeType, checksumType, typeof(long?) }, null);
			var c4p1 = Expression.Parameter(typeof(byte[]));
			var c4p2 = Expression.Parameter(typeof(int));
			var c4p3 = Expression.Parameter(typeof(int));
			var c4p4 = Expression.Parameter(typeof(LZ4FrameBlockMode));
			var c4p4_c = Expression.Convert(c4p4, blockModeType);
			var c4p5 = Expression.Parameter(typeof(LZ4FrameBlockSize));
			var c4p5_c = Expression.Convert(c4p5, blockSizeType);
			var c4p6 = Expression.Parameter(typeof(LZ4FrameChecksumMode));
			var c4p6_c = Expression.Convert(c4p6, checksumType);
			var c4p7 = Expression.Parameter(typeof(long?));
			var c4ce = Expression.Call(c4, c4p1, c4p2, c4p3, c4p4_c, c4p5_c, c4p6_c, c4p7);
			_compress4 = Expression.Lambda<Func<byte[], int, int, LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, byte[]>>(c4ce, c4p1, c4p2, c4p3, c4p4, c4p5, c4p6, c4p7).Compile();

			var d3 = helperType2.GetMethod("Decompress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]) }, null);
			var d3p1 = Expression.Parameter(typeof(byte[]));
			var d3ce = Expression.Call(d3, d3p1);
			_decompress3 = Expression.Lambda<Func<byte[], byte[]>>(d3ce, d3p1).Compile();

			var d4 = helperType1.GetMethod("Decompress", BindingFlags.Public | BindingFlags.Static, null, new Type[] { typeof(byte[]), typeof(int), typeof(int) }, null);
			var d4p1 = Expression.Parameter(typeof(byte[]));
			var d4p2 = Expression.Parameter(typeof(int));
			var d4p3 = Expression.Parameter(typeof(int));
			var d4ce = Expression.Call(d4, d4p1, d4p2, d4p3);
			_decompress4 = Expression.Lambda<Func<byte[], int, int, byte[]>>(d4ce, d4p1, d4p2, d4p3).Compile();

			DetectVCRuntime();

			return;
		}

		private static void DetectVCRuntime() {
#if DETECT_VC_RUNTIME
			string key;
			if (IntPtr.Size == 4) {
				key = "SOFTWARE\\Microsoft\\VisualStudio\\12.0\\VC\\Runtimes\\x86";
			}
			else if (IntPtr.Size == 8) {
				key = "SOFTWARE\\Microsoft\\VisualStudio\\12.0\\VC\\Runtimes\\x64";
			}
			else {
				return;
			}

			bool hasVC = false;
			using (var view32 = Microsoft.Win32.RegistryKey.OpenBaseKey(Microsoft.Win32.RegistryHive.LocalMachine, Microsoft.Win32.RegistryView.Registry32)) {
				using (var k = view32.OpenSubKey(key, false)) {
					if (k != null) {
						object x = k.GetValue("Installed", null);
						if (x is int) {
							hasVC = ((int)x == 1);
						}
					}
				}
			}

			if (!hasVC) {
				throw new Exception("The lz4 assembly requires the Microsoft Visual C++ 2013 runtime installed");
			}
#endif
		}

		private static Action<Stream> _writeEndFrame;
		public static Action<Stream> WriteEndFrame() {
			Ensure();
			return _writeEndFrame;
		}

		private static Action<Stream, int, byte[], int, int> _writeUserDataFrame;
		public static Action<Stream, int, byte[], int, int> WriteUserDataFrame() {
			Ensure();
			return _writeUserDataFrame;
		}

		private static Action<Stream, Action<object, LZ4UserDataFrameEventArgs>> _userFrameEvent;
		public static Action<Stream, Action<object, LZ4UserDataFrameEventArgs>> UserFrameEvent() {
			Ensure();
			return _userFrameEvent;
		}

		private static Func<Stream, long> _currentBlockCount;
		public static Func<Stream, long> CurrentBlockCount() {
			Ensure();
			return _currentBlockCount;
		}

		private static Func<Stream, long> _frameCount;
		public static Func<Stream, long> FrameCount() {
			Ensure();
			return _frameCount;
		}

		private static Func<Stream, LZ4StreamMode, LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, bool, Stream> _createCompressor;
		public static Func<Stream, LZ4StreamMode, LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, bool, Stream> CreateCompressor() {
			Ensure();
			return _createCompressor;
		}

		private static Func<Stream, LZ4StreamMode, bool, Stream> _createDecompressor;
		public static Func<Stream, LZ4StreamMode, bool, Stream> CreateDecompressor() {
			Ensure();
			return _createDecompressor;
		}

		private static Func<byte[], byte[]> _compress1;
		public static Func<byte[], byte[]> Compress1() {
			Ensure();
			return _compress1;
		}

		private static Func<byte[], int, int, int, byte[]> _compress2;
		public static Func<byte[], int, int, int, byte[]> Compress2() {
			Ensure();
			return _compress2;
		}

		private static Func<byte[], LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, byte[]> _compress3;
		public static Func<byte[], LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, byte[]> Compress3() {
			Ensure();
			return _compress3;
		}

		private static Func<byte[], int, int, LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, byte[]> _compress4;
		public static Func<byte[], int, int, LZ4FrameBlockMode, LZ4FrameBlockSize, LZ4FrameChecksumMode, long?, byte[]> Compress4() {
			Ensure();
			return _compress4;
		}

		private static Func<byte[], byte[]> _decompress1;
		public static Func<byte[], byte[]> Decompress1() {
			Ensure();
			return _decompress1;
		}

		private static Func<byte[], int, int, byte[]> _decompress2;
		public static Func<byte[], int, int, byte[]> Decompress2() {
			Ensure();
			return _decompress2;
		}

		private static Func<byte[], byte[]> _decompress3;
		public static Func<byte[], byte[]> Decompress3() {
			Ensure();
			return _decompress3;
		}

		private static Func<byte[], int, int, byte[]> _decompress4;
		public static Func<byte[], int, int, byte[]> Decompress4() {
			Ensure();
			return _decompress4;
		}

		private static Assembly LoadLZ4Assembly() {
			string path = new Uri(typeof(LZ4Loader).Assembly.CodeBase).LocalPath;
			int x = path.LastIndexOf('\\');
			path = path.Substring(0, x);

			if (IntPtr.Size == 4) {
				path += "\\lz4.x86.dll";
			}
			else if (IntPtr.Size == 8) {
				path += "\\lz4.x64.dll";
			}
			else {
				throw new NotSupportedException(IntPtr.Size.ToString());
			}

			if (File.Exists(path)) {
				// load from file
				return Assembly.LoadFrom(path);
			}
			else {
				// load from GAC
				return Assembly.Load(new AssemblyName("lz4, Version=1.0.4.0, Culture=neutral, PublicKeyToken=7aa3c636ef56b77f"));
			}
		}
	}
}
