/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NRE (NOVA runtime environment).
 *
 * NRE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NRE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#pragma once

#include <ipc/PtClientSession.h>
#include <ipc/Consumer.h>
#include <services/Network.h>
#include <mem/DataSpace.h>

namespace nre {

/**
 * Types for the vmmanager-service
 */
class VMManager {
public:
    enum Command {
        INIT,
        GEN_MAC,
    };
    enum Event {
        RESET,
        TERMINATE,
        KILL,
    };

    struct Packet {
        Event event;
    };
};

/**
 * Represents a session at the vmmanager service. This is intended for controlling vancouver
 * from the vmmanager. I.e. vmmanager provides this service and vancouver uses it and listens
 * for requests.
 */
class VMManagerSession : public PtClientSession {
    static const size_t DS_SIZE = ExecEnv::PAGE_SIZE;

public:
    /**
     * Creates a new session at given service
     *
     * @param service the service name
     */
    explicit VMManagerSession(const String &service)
        : PtClientSession(service), _ds(DS_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW), _sm(0),
          _consumer(_ds, _sm, true) {
        create();
    }

    /**
     * @return the consumer to receive commands from the vmmanager
     */
    Consumer<VMManager::Packet> &consumer() {
        return _consumer;
    }

    /**
     * Generates a unique MAC-address for this VM.
     *
     * @return the MAC-address
     */
    Network::EthernetAddr generate_mac() {
        Network::EthernetAddr res;
        UtcbFrame uf;
        uf << VMManager::GEN_MAC;
        Pt pt(caps() + CPU::current().log_id());
        pt.call(uf);
        uf.check_reply();
        uf >> res;
        return res;
    }

private:
    void create() {
        UtcbFrame uf;
        uf.delegate(_ds.sel(), 0);
        uf.delegate(_sm.sel(), 1);
        uf.translate(Pd::current()->sel());
        uf << VMManager::INIT;
        Pt pt(caps() + CPU::current().log_id());
        pt.call(uf);
        uf.check_reply();
    }

    DataSpace _ds;
    Sm _sm;
    Consumer<VMManager::Packet> _consumer;
};

}
