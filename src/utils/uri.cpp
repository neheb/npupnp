/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (c) 2012 France Telecom All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/


/*!
 * \file
 *
 * \brief Contains functions for uri, url parsing utility.
 */
#include "config.h"

#include <iostream>

#ifdef __FreeBSD__
#include <osreldate.h>
#if __FreeBSD_version < 601103
#include <lwres/netdb.h>
#endif
#endif
#include <assert.h>

#include "uri.h"
#include "smallut.h"
#include "upnpapi.h"

/*!
 * \brief Parses a string representing a host and port (e.g. "127.127.0.1:80"
 * or "localhost") and fills out a hostport_type struct with internet address
 * and a token representing the full host and port.
 *
 * Uses gethostbyname.
 */
static int parse_hostport(
	/*! [in] String of characters representing host and port. */
	const char *in,
	/*! [out] Output parameter where the host and port are represented as
	 * an internet address. */
	hostport_type *out)
{
	char workbuf[256];
	char *c;
	struct sockaddr_in *sai4 = (struct sockaddr_in *)&out->IPaddress;
	struct sockaddr_in6 *sai6 = (struct sockaddr_in6 *)&out->IPaddress;
	char *srvname = NULL;
	char *srvport = NULL;
	char *last_dot = NULL;
	unsigned short int port;
	int af = AF_UNSPEC;
	size_t hostport_size;
	int has_port = 0;
	int ret;

	*out = hostport_type();
	/* Work on a copy of the input string. */
	upnp_strlcpy(workbuf, in, sizeof(workbuf));
	c = workbuf;
	if (*c == '[') {
		/* IPv6 addresses are enclosed in square brackets. */
		srvname = ++c;
		while (*c != '\0' && *c != ']')
			c++;
		if (*c == '\0')
			/* did not find closing bracket. */
			return UPNP_E_INVALID_URL;
		/* NULL terminate the srvname and then increment c. */
		*c++ = '\0';	/* overwrite the ']' */
		if (*c == ':') {
			has_port = 1;
			c++;
		}
		af = AF_INET6;
	} else {
		/* IPv4 address -OR- host name. */
		srvname = c;
		while (*c != ':' && *c != '/' &&
			   (isalnum(*c) || *c == '.' || *c == '-')) {
			if (*c == '.')
				last_dot = c;
			c++;
		}
		has_port = (*c == ':') ? 1 : 0;
		/* NULL terminate the srvname */
		*c = '\0';
		if (has_port == 1)
			c++;
		if (last_dot != NULL && isdigit(*(last_dot + 1)))
			/* Must be an IPv4 address. */
			af = AF_INET;
		else {
			/* Must be a host name. */
			struct addrinfo hints, *res, *res0;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;

			ret = getaddrinfo(srvname, NULL, &hints, &res0);
			if (ret == 0) {
				for (res = res0; res; res = res->ai_next) {
					switch (res->ai_family) {
					case AF_INET:
					case AF_INET6:
						/* Found a valid IPv4 or IPv6 address. */
						memcpy(&out->IPaddress,
							   res->ai_addr,
							   res->ai_addrlen);
						goto found;
					}
				}
			found:
				freeaddrinfo(res0);
				if (res == NULL)
					/* Didn't find an AF_INET or AF_INET6 address. */
					return UPNP_E_INVALID_URL;
			} else
				/* getaddrinfo failed. */
				return UPNP_E_INVALID_URL;
		}
	}
	/* Check if a port is specified. */
	if (has_port == 1) {
		/* Port is specified. */
		srvport = c;
		while (*c != '\0' && isdigit(*c))
			c++;
		port = (unsigned short int)atoi(srvport);
		if (port == 0)
			/* Bad port number. */
			return UPNP_E_INVALID_URL;
	} else
		/* Port was not specified, use default port. */
		port = 80u;
	/* The length of the host and port string can be calculated by */
	/* subtracting pointers. */
	hostport_size = (size_t)c - (size_t)workbuf;
	/* Fill in the 'out' information. */
	switch (af) {
	case AF_INET:
		sai4->sin_family = (sa_family_t)af;
		sai4->sin_port = htons(port);
		ret = inet_pton(AF_INET, srvname, &sai4->sin_addr);
		break;
	case AF_INET6:
		sai6->sin6_family = (sa_family_t)af;
		sai6->sin6_port = htons(port);
		sai6->sin6_scope_id = gIF_INDEX;
		ret = inet_pton(AF_INET6, srvname, &sai6->sin6_addr);
		break;
	default:
		/* IP address was set by the hostname (getaddrinfo). */
		/* Override port: */
		if (out->IPaddress.ss_family == (sa_family_t)AF_INET)
			sai4->sin_port = htons(port);
		else
			sai6->sin6_port = htons(port);
		ret = 1;
	}
	/* Check if address was converted successfully. */
	if (ret <= 0)
		return UPNP_E_INVALID_URL;
	out->text.assign(in, hostport_size);

	return (int)hostport_size;
}

/*!
 * \brief parses a uri scheme starting at in[0] as defined in 
 * http://www.ietf.org/rfc/rfc2396.txt (RFC explaining URIs).
 *
 * (e.g. "http:" -> scheme= "http").
 *
 * \note String MUST include ':' within the max charcters.
 *
 * \return 
 */
static size_t parse_scheme(const std::string& in, std::string& out)
{
	out.clear();

	// A scheme begins with an alphabetic character
	if (in.empty() || !isalpha(in[0]))
		return 0;

	// Need a colon
	std::string::size_type colon = in.find(':');
	if (colon == std::string::npos) {
		return 0;
	}
	// Check contents: "[::alphanum::+-.]*:"
	for (size_t i = 0; i < colon; i++) {
		if (!(isalnum(in[i]) || in[i] == '+' || in[i] == '-' || in[i] == '.'))
			return 0;
	}
	out = in.substr(0, colon);
	return out.size();
}


/*!
 * \brief Replaces an escaped sequences with theur unescaped version as in
 * http://www.ietf.org/rfc/rfc2396.txt	(RFC explaining URIs)
 */
static inline int h2d(int c)
{
	if ('0' <= c && c <= '9')
		return c - '0';
	else if ('A' <= c && c <= 'F')
		return 10 + c - 'A';
	else 
		return -1;
}

std::string remove_escaped_chars(const std::string& in)
{
	if (in.size() <= 2)
		return in;
	std::string out;
	out.reserve(in.size());
	size_t i = 0;
	for (; i < in.size() - 2; i++) {
		if (in[i] == '%') {
			int d1 = h2d(in[i+1]);
			int d2 = h2d(in[i+2]);
			if (d1 != -1 && d2 != -1) {
				out += (d1 << 4) + d2;
			} else {
				out += '%';
				out += in[i+1];
				out += in[i+2];
			}
			i += 2;
		} else {
			out += in[i];
		}
	}
	while (i < in.size()) {
		out += in[i++];
	}
	return out;
}


std::string remove_dots(const std::string& in)
{
    static const std::string markers("/?");
    std::vector<std::string> vpath;
    if (in.empty()) {
        return in;
    }
    bool isabs = in[0] == '/';
    bool endslash = in.back() == '/';
    std::string::size_type pos = 0;
    while (pos != std::string::npos) {
        std::string::size_type epos = in.find_first_of(markers, pos);
        if (epos != std::string::npos && in[epos] == '?') {
            // done
            epos = std::string::npos;
        }
        if (epos == pos) {
            pos++;
            continue;
        }
        std::string elt = (epos == std::string::npos) ?
            in.substr(pos) : in.substr(pos, epos - pos);
        if (elt.empty() || elt == ".") {
            // Do nothing, // or /./ are ignored
        } else if (elt == "..") {
            if (vpath.empty()) {
                // This is an error: trying to go behind /
                return std::string();
            } else {
                vpath.pop_back();
            }
        } else {
            vpath.push_back(elt);
        }
        pos = epos;
    }
    std::string out = isabs ? "/" : "";
    for (const auto& elt : vpath) {
        out += elt + "/";
    }
    // Pop the last / if the original path did not end with /
    if (!endslash && out.size() > 1 && out.back() == '/')
        out.pop_back();
    return out;
}

std::string resolve_rel_url(
	const std::string& base_url, const std::string& rel_url)
{
	uri_type base;
	uri_type rel;
	uri_type url;

	// Base can't be empty, it needs at least a scheme.
	if (base_url.empty()) {
		return std::string();
	}
	if ((parse_uri(base_url, &base) != UPNP_E_SUCCESS)
		|| (base.type != URITP_ABSOLUTE)) {
		return std::string();
	}
	if (rel_url.empty())
		return base_url;

	if (parse_uri(rel_url, &rel) != UPNP_E_SUCCESS) {
		return std::string();
	}

	rel.path = remove_dots(rel.path);

	if (rel.type == URITP_ABSOLUTE) {
		return uri_asurlstr(rel);
	}

	url.scheme = base.scheme;
	url.fragment = rel.fragment;

	if (!rel.hostport.text.empty()) {
        url.hostport = rel.hostport;
		url.path = rel.path;
		url.query = rel.query;
        return uri_asurlstr(url);
	}

	url.hostport = base.hostport;

	if (rel.path.empty()) {
		url.path = base.path;
		if (!rel.query.empty()) {
			url.query = rel.query;
		} else {
			url.query = base.query;
		}
	} else {
		if (rel.path[0] == '/') {
			url.path = rel.path;
		} else {
			// Merge paths
			if (base.path.empty()) {
				url.path = std::string("/") + rel.path;
			} else {
				if (base.path == "/") {
					url.path = base.path + rel.path;
				} else {
					if (base.path.back() == '/') {
						base.path.pop_back();
					}
					std::string::size_type pos = base.path.rfind("/");
					url.path = base.path.substr(0, pos+1) + rel.path;
				}
				url.query = rel.query;
			}
		}
	}
	return uri_asurlstr(url);
}

int parse_uri(const std::string& in, uri_type *out)
{
	size_t begin_hostport = parse_scheme(in, out->scheme);
	if (begin_hostport) {
		out->type = URITP_ABSOLUTE;
		out->path_type = OPAQUE_PART;
		begin_hostport++; // Skip ':'
	} else {
		out->type = URITP_RELATIVE;
		out->path_type = REL_PATH;
	}

	int begin_path = 0;
	if (begin_hostport + 1 < in.size() && in[begin_hostport] == '/' &&
		in[begin_hostport + 1] == '/') {
		begin_hostport += 2;
		begin_path = parse_hostport(in.c_str() + begin_hostport, &out->hostport);
		if (begin_path >= 0) {
			begin_path += begin_hostport;
		} else {
			return begin_path;
		}
	} else {
		begin_path = (int)begin_hostport;
	}
	std::string::size_type question = in.find('?', begin_path);
	std::string::size_type hash = in.find('#', begin_path);
	if (question == std::string::npos &&
		hash == std::string::npos) {
		out->path = in.substr(begin_path);
	} else if (question != std::string::npos && hash == std::string::npos) {
		out->path = in.substr(begin_path, question - begin_path);
		out->query = in.substr(question+1);
	} else if (question == std::string::npos && hash != std::string::npos) {
		out->path = in.substr(begin_path, hash - begin_path);
		out->fragment = in.substr(hash+1);
	} else {
		if (hash < question) {
			out->path = in.substr(begin_path, hash - begin_path);
			out->fragment = in.substr(hash+1);
		} else {
			out->path = in.substr(begin_path, question - begin_path);
			out->query = in.substr(question + 1, hash - question - 1);
			out->fragment = in.substr(hash+1);
		}
	}

	if (!out->path.empty() && out->path[0] == '/') {
		out->path_type = ABS_PATH;
	}

	return UPNP_E_SUCCESS;
}