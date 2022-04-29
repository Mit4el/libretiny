#include "WiFiServer.h"
#include "WiFiPriv.h"

WiFiServer::WiFiServer(uint16_t port, uint8_t maxClients)
	: _sock(-1), _sockAccepted(-1), _port(port), _maxClients(maxClients), _active(false), _noDelay(false) {}

WiFiServer::operator bool() {
	return _active;
}

bool WiFiServer::begin(uint16_t port, bool reuseAddr) {
	if (_active)
		return true;
	if (port)
		_port = port;

	_sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (_sock < 0) {
		LT_E("Socket failed; errno=%d", errno);
		return false;
	}

	int enable = reuseAddr;
	lwip_setsockopt(_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

	struct sockaddr_in addr;
	addr.sin_family		 = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port		 = htons(_port);

	if (lwip_bind(_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LT_E("Bind failed; errno=%d", errno);
		return false;
	}

	if (lwip_listen(_sock, _maxClients) < 0) {
		LT_E("Bind failed; errno=%d", errno);
		return false;
	}

	LT_I("Server running on :%hu", _port);

	lwip_fcntl(_sock, F_SETFL, O_NONBLOCK);
	_active		  = true;
	_noDelay	  = false;
	_sockAccepted = -1;
}

void WiFiServer::end() {
	if (_sock == -1)
		return;
	lwip_close(_sock);
	_sock	= -1;
	_active = -1;
}

WiFiClient WiFiServer::accept() {
	if (!_active)
		return WiFiClient();

	int sock;
	if (_sockAccepted >= 0) {
		sock		  = _sockAccepted;
		_sockAccepted = -1;
	} else {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		sock		  = lwip_accept(_sock, (struct sockaddr *)&addr, &len);
	}

	if (sock >= 0) {
		int enable = 1;
		if (lwip_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) == ERR_OK) {
			enable = _noDelay;
			if (lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) == ERR_OK) {
				// HOTFIX: allow the TCP thread to receive data
				// I'm not sure what's happening there, so this should probably be fixed properly.
				// When a connection arrives, sometimes TCP hasn't received anything yet. This causes
				// calling WiFiClient::connected() check for lwip_recv(), which returns EWOULDBLOCK
				// as the client is still connected. The problem is that there's basically an infinite loop
				// created: nowhere in that code is a yield()/delay() that would allow TCP thread to work
				// and receive data, so LwIP still sees a connected client that sends nothing. At least
				// that's what I understand. And any loop that doesn't call delay() seems to block the TCP
				// stack completely and prevents it from even being pinged.
				LT_D_WS("Got client");
				delay(5);
				return WiFiClient(sock);
			}
		}
	}

	return WiFiClient();
}

int WiFiServer::setTimeout(uint32_t seconds) {
	struct timeval tv;
	tv.tv_sec  = seconds;
	tv.tv_usec = 0;
	if (lwip_setsockopt(_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		return -1;
	return lwip_setsockopt(_sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

void WiFiServer::setNoDelay(bool noDelay) {
	_noDelay = noDelay;
}

bool WiFiServer::getNoDelay() {
	return _noDelay;
}

bool WiFiServer::hasClient() {
	if (_sockAccepted >= 0) {
		return true;
	}
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	_sockAccepted = lwip_accept(_sock, (struct sockaddr *)&addr, &len);
	if (_sockAccepted >= 0) {
		return true;
	}
	return false;
}