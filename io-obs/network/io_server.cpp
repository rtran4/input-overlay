/**
 * This file is part of input-overlay
 * which is licensed under the MPL 2.0 license
 * See LICENSE or mozilla.org/en-US/MPL/2.0/
 * github.com/univrsal/input-overlay
 */

#include "io_server.hpp"
#include "remote_connection.hpp"
#include "util/util.hpp"
#include <obs-module.h>
#include <util/platform.h>
#include <algorithm>
#include "../util/element/element_button.hpp"

static netlib_socket_set sockets = nullptr;

namespace network
{

    io_server::io_server(const uint16_t port)
        : m_server(nullptr)
    {
        sockets = nullptr;
        m_num_clients = 0;
        m_ip.port = port;
    }

    io_server::~io_server()
    {
        /* Smart pointer will delete 
         * and destructor will close socket
         */
        m_clients.clear();
    }

    bool io_server::init()
    {
		auto flag = true;

        if (netlib_resolve_host(&m_ip, nullptr, m_ip.port) == -1)
        {
            LOG_(LOG_ERROR, "netlib_resolve_host failed: %s.", netlib_get_error());
			flag = false;
        }
        else
        {
			const auto ipaddr = netlib_swap_BE32(m_ip.host);
			LOG_(LOG_INFO, "Remote connection open on %d.%d.%d.%d:%hu",
				ipaddr >> 24, ipaddr >> 16 & 0xff, ipaddr >> 8 & 0xff, ipaddr & 0xff, m_ip.port);

			m_server = netlib_tcp_open(&m_ip);
			m_buffer = netlib_alloc_byte_buf(BUFFER_SIZE);

            if (!m_buffer)
            	LOG_(LOG_ERROR, "netlib_alloc_byte_buf failed; %s", netlib_get_error());
            
			if (!m_server)
			{
				LOG_(LOG_ERROR, "netlib_tcp_open failed: %s", netlib_get_error());
				flag = false;
			}
		}
		return flag;
    }

    void io_server::listen(int& numready)
    {
        if (create_sockets())
            numready = netlib_check_socket_set(sockets, 100);
    }

    tcp_socket io_server::socket() const
    {
        return m_server;
    }
    
    void io_server::update_clients()
    {
		uint8_t id = 0;

#ifdef _DEBUG
		uint64_t last_msg;
#endif

        for (const auto& client : m_clients)
        {
            if (netlib_socket_ready(client->socket()))
            {
                /* Receive input data */
				m_buffer->read_pos = 0; /* Reset buffer */

                if (!netlib_tcp_recv_buf(client->socket(), m_buffer))
                {
				    LOG_(LOG_ERROR, "Failed to receive buffer from %s. Closed connection", client->name());
                    client->mark_invalid();
                    continue;
                }

                const auto msg = read_msg_from_buffer(m_buffer);
				
                switch (msg)
                {
				case MSG_PREVENT_TIMEOUT:
                    last_msg = client->last_message() / (1000 * 1000);
#ifdef _DEBUG 
                    
					LOG_(LOG_INFO, "Received refresh message from %s after %ums.", client->name(), last_msg);
#endif
                    /* Sockets can get stuck after incorrect DC
                     * So if the message is received at an unusual speed
                     * just disconnect the client
                     */
                    if (client->last_message() < TIMEOUT_NS / 2)
                    {
                        LOG_(LOG_INFO, "Recieved refresh message from %s at unusual speed(%ums). Disconnecting.",
                            client->name(), last_msg);
                        client->mark_invalid();
                    }
                    else
                    {
                        client->reset_timeout();
                    }
					break;
				case MSG_MOUSE_POS_DATA:
				case MSG_BUTTON_DATA:
					client->reset_timeout();
					if (!client->read_event(m_buffer, msg))
					{
						LOG_(LOG_ERROR, "Failed to receive event data from %s. Closed connection", client->name());
						/* TODO: Disconnect routine */
					}
					break;
                case MSG_CLIENT_DC:
                    client->mark_invalid();
                    break;
				case MSG_INVALID:
					break;
				default: ;
                }
            }
			id++;
        }
    }

	void io_server::get_clients(std::vector<const char*>& v)
    {

		for (const auto& client : m_clients)
		{
			v.emplace_back(client->name());
		}
		m_clients_changed = false;
    }

    void io_server::get_clients(obs_property_t* prop, const bool enable_local)
    {
		obs_property_list_clear(prop);

		if (enable_local)
			obs_property_list_add_int(prop, T_LOCAL_SOURCE, 0);

		for (const auto& client : m_clients)
		{
			obs_property_list_add_int(prop, client->name(), client->id() + 1); /* 0 is for local input */
		}
    }

    bool io_server::clients_changed() const
    {
		return m_clients_changed;
    }
    
	void io_server::roundtrip()
	{
		if (!m_clients.empty())
		{
			auto old = m_clients.size();
            m_clients.erase(std::remove_if(
			    m_clients.begin(), m_clients.end(),
                [](const std::unique_ptr<io_client>& o)
                {
                    if (!o->valid())
                    {
						network::server_instance->m_num_clients--;
						LOG_(LOG_INFO, "%s disconnected. Invalid socket.", o->name());
						return true;
                    }
                    if (o->last_message() > TIMEOUT_NS)
                    {
						network::server_instance->m_num_clients--;
						LOG_(LOG_INFO, "%s disconnected due to timeout.", o->name());
						return true;
                    }
					return false;
                }
			), m_clients.end());

			if (old != m_clients.size())
				m_clients_changed = true;
		}
	}

    io_client* io_server::get_client(const uint8_t id)
    {
		if (id >= 0 && id < m_clients.size())
			return m_clients[id].get();
		return nullptr;
    }

    void io_server::add_client(tcp_socket socket, char* name)
    {
		fix_name(name);

        if (!strlen(name))
        {
            LOG_(LOG_INFO, "Disconnected %s: Invalid name", name);
			send_message(socket, MSG_NAME_INVALID);
            netlib_tcp_close(socket);
            return;
        }

        if (!unique_name(name))
        {
            LOG_(LOG_INFO, "Disconnected %s: Name already in use", name);
			send_message(socket, MSG_NAME_NOT_UNIQUE);
            netlib_tcp_close(socket);
            return;
        }

		LOG_(LOG_INFO, "Received connection from '%s'.", name);

		m_clients_changed = true;
        m_clients.emplace_back(new io_client(name, socket, m_num_clients));
        m_num_clients++;
    }

    bool io_server::unique_name(char* name)
    {
        if (!name)
            return false;
        auto flag = true;
        for (auto& client : m_clients)
        {
            if (!strcmp(name, client->name()))
            {
                flag = false;
                break;
            }
        }
        return flag;
    }

    /* Only works with prealloccated char arrays */
    void io_server::fix_name(char* name)
    {
		static auto accepted = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-0123456789";
		size_t pos;
        const auto len = strlen(name);
		while ((pos = strspn(name, accepted)) != len)
		{
			name[pos] = '_';
		}
    }

    bool io_server::create_sockets()
    {
        int i;
        if (sockets)
            netlib_free_socket_set(sockets);

        sockets = netlib_alloc_socket_set(m_num_clients + 1);
        if (!sockets)
        {
            LOG_(LOG_ERROR, "netlib_alloc_socket_set failed with %i clients.", m_num_clients + 1);
            network_flag = false;
            return false;
        }

        netlib_tcp_add_socket(sockets, m_server);

        for (const auto& client : m_clients)
            netlib_tcp_add_socket(sockets, client->socket());

        return true;
    }
}
