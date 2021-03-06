﻿/*
 * zsummerX License
 * -----------
 * 
 * zsummerX is licensed under the terms of the MIT license reproduced below.
 * This means that zsummerX is free software and can be used for both academic
 * and commercial purposes at absolutely no cost.
 * 
 * 
 * ===============================================================================
 * 
 * Copyright (C) 2013 YaweiZhang <yawei_zhang@foxmail.com>.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * ===============================================================================
 * 
 * (end of COPYRIGHT)
 */


#include <zsummerX/epoll/tcpsocket_impl.h>
#include <zsummerX/epoll/tcpaccept_impl.h>


using namespace zsummer::network;




CTcpAcceptImpl::CTcpAcceptImpl()
{
	m_register._event.data.ptr = &m_register;
	m_register._ptr = this;
	m_register._type = tagRegister::REG_TCP_ACCEPT;
}


CTcpAcceptImpl::~CTcpAcceptImpl()
{
	if (m_register._fd != InvalideFD)
	{
		close(m_register._fd);
		m_register._fd = InvalideFD;
	}
}
std::string CTcpAcceptImpl::GetAcceptImplStatus()
{
	std::stringstream os;
	os << " CTcpAcceptImpl:Status m_summer=" << m_summer.use_count() << ", m_listenIP=" << m_listenIP
		<< ", m_listenPort=" << m_listenPort << ", m_onAcceptHandler=" << (bool)m_onAcceptHandler
		<< ", m_client=" << m_client.use_count() << "m_register=" << m_register;
	return os.str();
}
bool CTcpAcceptImpl::Initialize(CZSummerPtr summer)
{
	m_summer = summer;
	m_register._linkstat = LS_WAITLINK;
	return true;
}


bool CTcpAcceptImpl::OpenAccept(std::string listenIP, unsigned short listenPort)
{
	if (!m_summer)
	{
		LCE("CTcpAcceptImpl::OpenAccept[this0x" << this << "] m_summer not bind!" << GetAcceptImplStatus());
		return false;
	}

	if (m_register._fd != InvalideFD)
	{
		LCF("CTcpAcceptImpl::OpenAccept[this0x" << this << "] accept fd is aready used!" << GetAcceptImplStatus());
		return false;
	}

	m_register._fd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_register._fd == InvalideFD)
	{
		LCF("CTcpAcceptImpl::OpenAccept[this0x" << this << "] listen socket create err errno =" << strerror(errno) << GetAcceptImplStatus());
		return false;
	}

	SetNonBlock(m_register._fd);



	int bReuse = 1;
	if (setsockopt(m_register._fd, SOL_SOCKET, SO_REUSEADDR,  &bReuse, sizeof(bReuse)) != 0)
	{
		LCW("CTcpAcceptImpl::OpenAccept[this0x" << this << "] listen socket set reuse fail! errno=" << strerror(errno) << GetAcceptImplStatus());
	}


	m_addr.sin_family = AF_INET;
	m_addr.sin_addr.s_addr = inet_addr(listenIP.c_str());
	m_addr.sin_port = htons(listenPort);
	if (bind(m_register._fd, (sockaddr *) &m_addr, sizeof(m_addr)) != 0)
	{
		LCF("CTcpAcceptImpl::OpenAccept[this0x" << this << "] listen socket bind err, errno=" << strerror(errno) << GetAcceptImplStatus());
		close(m_register._fd);
		m_register._fd = InvalideFD;
		return false;
	}

	if (listen(m_register._fd, 200) != 0)
	{
		LCF("CTcpAcceptImpl::OpenAccept[this0x" << this << "] listen socket listen err, errno=" << strerror(errno) << GetAcceptImplStatus());
		close(m_register._fd);
		m_register._fd = InvalideFD;
		return false;
	}

	if (!m_summer->m_impl.RegisterEvent(EPOLL_CTL_ADD, m_register))
	{
		LCF("CTcpAcceptImpl::OpenAccept[this0x" << this << "] listen socket register error." << GetAcceptImplStatus());
		return false;
	}
	m_register._linkstat = LS_ESTABLISHED;
	return true;
}

bool CTcpAcceptImpl::DoAccept(CTcpSocketPtr &s, const _OnAcceptHandler &handle)
{
	if (m_onAcceptHandler)
	{
		LCF("CTcpAcceptImpl::DoAccept[this0x" << this << "] err, dumplicate DoAccept" << GetAcceptImplStatus());
		return false;
	}
	if (m_register._linkstat != LS_ESTABLISHED)
	{
		LCF("CTcpAcceptImpl::DoAccept[this0x" << this << "] err, _linkstat not work state" << GetAcceptImplStatus());
		return false;
	}
	
	m_register._event.events = EPOLLIN;
	if (!m_summer->m_impl.RegisterEvent(EPOLL_CTL_MOD, m_register))
	{
		LCF("CTcpAcceptImpl::DoAccept[this0x" << this << "] err, _linkstat not work state" << GetAcceptImplStatus());
		return false;
	}

	m_onAcceptHandler = handle;
	m_client = s;
	return true;
}
void CTcpAcceptImpl::OnEPOLLMessage(bool bSuccess)
{
	//
	if (!m_onAcceptHandler)
	{
		LCF("CTcpAcceptImpl::DoAccept[this0x" << this << "] err, dumplicate DoAccept" << GetAcceptImplStatus());
		return ;
	}
	if (m_register._linkstat != LS_ESTABLISHED)
	{
		LCF("CTcpAcceptImpl::DoAccept[this0x" << this << "] err, _linkstat not work state" << GetAcceptImplStatus());
		return ;
	}

	_OnAcceptHandler onAccept;
	onAccept.swap(m_onAcceptHandler);
	CTcpSocketPtr ps(m_client);
	m_client.reset();
	m_register._event.events = 0;
	m_summer->m_impl.RegisterEvent(EPOLL_CTL_MOD, m_register);

	if (!bSuccess)
	{
		LCF("CTcpAcceptImpl::DoAccept[this0x" << this << "]  EPOLLERR, errno=" << strerror(errno) << GetAcceptImplStatus());
		onAccept(EC_ERROR, ps);
		return ;
	}
	else
	{
		sockaddr_in cltaddr;
		memset(&cltaddr, 0, sizeof(cltaddr));
		socklen_t len = sizeof(cltaddr);
		int s = accept(m_register._fd, (sockaddr *)&cltaddr, &len);
		if (s == -1)
		{
			if (errno != EAGAIN && errno != EWOULDBLOCK)
			{
				LCE("CTcpAcceptImpl::DoAccept[this0x" << this << "] ERR: accept return -1, errno=" << strerror(errno) << GetAcceptImplStatus());
			}
			onAccept(EC_ERROR, ps);
			return ;
		}

		SetNonBlock(s);
		SetNoDelay(s);

		ps->m_impl.AttachEstablishedSocket(s,inet_ntoa(cltaddr.sin_addr), ntohs(cltaddr.sin_port));
		onAccept(EC_SUCCESS, ps);
	}
	return ;
}

bool CTcpAcceptImpl::Close()
{
	m_onAcceptHandler = nullptr;
	m_summer->m_impl.RegisterEvent(EPOLL_CTL_DEL, m_register);
	shutdown(m_register._fd, SHUT_RDWR);
	close(m_register._fd);
	m_register._fd = InvalideFD;
	return true;
}

