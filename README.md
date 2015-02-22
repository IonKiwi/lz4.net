LZ4 for .NET (C++ /CLI)
=======================

LZ4 has been written by Yann Collet and the original sources can be found on http://code.google.com/p/lz4/  
This project contains the original C sources recompiled for (Pure) CLR  

How to use
----------------------------

Project contains 3 ways of using the library


**non-streaming**

Non streaming version, works with managed byte arrays  

Example:  

```csharp
  // compress data
  byte[] dataToCompress = ...
  byte[] compressedData = LZ4Helper.Compress(dataToCompress);
  // decompress data
  byte[] decompressedData = LZ4Helper.Decompress(compressedData);
```


**streaming**

Streaming version, using the LZ4 Framing Format (v1.4.1).  
The compressed data that this stream creates is readable by the lz4 command line tools.  

Example:  

```csharp
  // compress data [with content checksum]
  using (LZ4Stream stream = LZ4Stream.CreateCompressor(innerStream, LZ4StreamMode.Write, LZ4FrameBlockMode.Linked, LZ4FrameBlockSize.Max64KB, LZ4FrameChecksumMode.Content, null, false)) {
    // write uncompressed data to the lz4 stream
	// the stream will compress the data and write it to the innerStream
	stream.Write(buffer, 0, buffer.Length);	
  }
  
  // compress data [with block and content checksum, start a new frame after 100 data blocks]
  // note: the lz4 command line tools do NOT support block checksums currently
  using (LZ4Stream stream = LZ4Stream.CreateCompressor(innerStream, LZ4StreamMode.Write, LZ4FrameBlockMode.Linked, LZ4FrameBlockSize.Max64KB, LZ4FrameChecksumMode.Block | LZ4FrameChecksumMode.Content, 100, false)) {
    // write uncompressed data to the lz4 stream
	// the stream will compress the data and write it to the innerStream
	stream.Write(buffer, 0, buffer.Length);	
  }
  
  // decompress data (in read mode)
  using (LZ4Stream stream = LZ4Stream.CreateDecompressor(innerStream, LZ4StreamMode.Read, false)) {
    // the lz4 stream will read the compressed data from the innerStream
    // and return the uncompressed data in 'buffer'
	int bytesRead = stream.Read(buffer, 0, buffer.Length);
  }
  
  // decompress data (in write mode)
  using (LZ4Stream stream = LZ4Stream.CreateDecompressor(innerStream, LZ4StreamMode.Write, false)) {
    // the lz4 stream will decompress the data from 'buffer'
	// and write the uncompressed data to the 'innerStream'
	stream.Write(buffer, 0, buffer.Length);
  }
```


**minimal streaming**

Streaming version using a custom minimal frame format.  
Based on 'blockStreaming_ringBuffer.c' example from the original lz4 sources.  

Compressed data is NOT readable by the lz4 command line tools.  

Example:  

```csharp
  // compress data
  using (LZ4MinimalFrameFormatStream stream = new LZ4MinimalFrameFormatStream(tmpFile1, CompressionMode.Compress) {
	// write uncompressed data to the lz4 stream
	// the stream will compress the data and write it to the innerStream
	stream.Write(buffer, 0, buffer.Length);	
  }
  
  // decompress data
  using (LZ4MinimalFrameFormatStream stream = new LZ4MinimalFrameFormatStream(innerStream, CompressionMode.Decompress)) {
    // the lz4 stream will read the compressed data from the innerStream
    // and return the uncompressed data in 'buffer'
	int bytesRead = stream.Read(buffer, 0, buffer.Length)
  }
```

NOTE:
 - LZ4MinimalFrameFormatStream contains a ctor to specify the size of the ringbuffer, the ringbuffer used for encoding and decoding need to be sychronized, i.e. use the same (ring)buffer sizes for encoding and decoding.  
 - Although the frame format of LZ4MinimalFrameFormatStream is shorter, it is probably better to use LZ4Stream as it has more options (like a content checksum) and is readable by the lz4 command line tools.  
