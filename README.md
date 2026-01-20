# Hanzo KV

Ultra-fast key-value store with Redis compatibility.

## Overview

Hanzo KV is a high-performance key-value store that provides Redis protocol compatibility while delivering enhanced performance and reliability. Perfect for caching, session storage, real-time analytics, and message queuing in AI applications.

## Features

- **Redis Compatible**: Drop-in replacement with full protocol support
- **High Performance**: Optimized memory management and I/O
- **Persistence**: RDB and AOF persistence options
- **Clustering**: Native cluster mode for horizontal scaling
- **Pub/Sub**: Real-time messaging capabilities
- **Lua Scripting**: Server-side scripting support

## Quick Start

```bash
docker run -p 6379:6379 hanzo/kv
```

## Documentation

See the [documentation](https://hanzo.ai/docs/kv) for detailed guides and API reference.

## License

MIT License - see [LICENSE](LICENSE) for details.
