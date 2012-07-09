/*
 * Main code and static vars of vancouver.nova.
 *
 * Copyright (C) 2007-2010, Bernhard Kauer <bk@vmmon.org>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of Vancouver.
 *
 * Vancouver.nova is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * Vancouver.nova is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include <kobj/Sm.h>
#include <kobj/Ports.h>
#include <dev/Reboot.h>
#include <util/TimeoutList.h>
#include <util/Util.h>

#include "bus/motherboard.h"
#include "bus/vcpu.h"
#include "Vancouver.h"
#include "Timeouts.h"
#include "VCPUBackend.h"

using namespace nul;

static size_t ncpu = 1;
nul::UserSm globalsm(0);

PARAM_ALIAS(PC_PS2, "an alias to create an PS2 compatible PC",
	     " mem:0,0xa0000 mem:0x100000 ioio nullio:0x80 pic:0x20,,0x4d0 pic:0xa0,2,0x4d1"
	     " pit:0x40,0 scp:0x92,0x61 kbc:0x60,1,12 keyb:0,0x10000 mouse:1,0x10001 rtc:0x70,8"
	     " serial:0x3f8,0x4,0x4711 hostsink:0x4712,80 vga:0x03c0"
	     " vbios_disk vbios_keyboard vbios_mem vbios_time vbios_reset vbios_multiboot"
	     " msi ioapic pcihostbridge:0,0x10,0xcf8,0xe0000000 pmtimer:0x8000 vcpus")

PARAM_HANDLER(ncpu, "ncpu - change the number of vcpus that are created" ) {
	ncpu = argv[0];
}
PARAM_HANDLER(vcpus, " vcpus - instantiate the vcpus defined with 'ncpu'") {
	for(unsigned count = 0; count < ncpu; count++)
		mb.parse_args("vcpu halifax vbios lapic");
}

void Vancouver::reset() {
	Serial::get().writef("RESET device state\n");
	MessageLegacy msg2(MessageLegacy::RESET, 0);
	_mb.bus_legacy.send_fifo(msg2);
	globalsm.up();
}

bool Vancouver::receive(CpuMessage &msg) {
	if(msg.type != CpuMessage::TYPE_CPUID)
		return false;

	// XXX locking?
	// XXX use the reserved CPUID regions
	switch(msg.cpuid_index) {
		case 0x40000020:
			// NOVA debug leaf
			// TODO nova_syscall(15,msg.cpu->ebx,0,0,0);
			break;
		case 0x40000021:
			// Vancouver debug leaf
			_mb.dump_counters();
			break;
		case 0x40000022: {
			// time leaf
			timevalue_t tsc = Util::tsc();
			msg.cpu->eax = tsc;
			msg.cpu->edx = tsc >> 32;
			msg.cpu->ecx = Hip::get().freq_tsc;
		}
		break;

		default:
			/*
			 * We have to return true here, to make handle_vcpu happy.
			 * The values are already set in VCpu.
			 */
			return true;
	}
	return true;
}

bool Vancouver::receive(MessageHostOp &msg) {
	bool res = true;
	switch(msg.type) {
		case MessageHostOp::OP_ALLOC_IOIO_REGION: {
			new Ports(msg.value >> 8,msg.value & 0xff);
			Serial::get().writef("alloc ioio region %lx\n",msg.value);
		}
		break;

		case MessageHostOp::OP_ALLOC_IOMEM: {
			DataSpace *ds = new DataSpace(msg.len,DataSpaceDesc::LOCKED,DataSpaceDesc::RW,msg.value);
			msg.ptr = reinterpret_cast<char*>(ds->virt());
		}
		break;

		case MessageHostOp::OP_GUEST_MEM:
			if(msg.value >= _guest_size)
				msg.value = 0;
			else {
				msg.len = _guest_size - msg.value;
				msg.ptr = reinterpret_cast<char*>(_guest_mem.virt() + msg.value);
			}
			break;

		case MessageHostOp::OP_ALLOC_FROM_GUEST:
			assert((msg.value & 0xFFF) == 0);
			if(msg.value <= _guest_size) {
				_guest_size -= msg.value;
				msg.phys = _guest_size;
				Serial::get().writef("Allocating from guest %08lx+%lx\n",_guest_size,msg.value);
			}
			else
				res = false;
			break;

		case MessageHostOp::OP_NOTIFY_IRQ:
			assert(false);
			// TODO res = NOVA_ESUCCESS == nova_semup(_shared_sem[msg.value & 0xff]);
			break;

		case MessageHostOp::OP_ASSIGN_PCI:
			assert(false);
			/* TODO res = !Sigma0Base::hostop(msg);
			_dpci |= res;
			Logging::printf("%s\n",_dpci ? "DPCI device assigned" : "DPCI failed");*/
			break;

		case MessageHostOp::OP_GET_MODULE: {
			// TODO that's extremly hardcoded here ;)
			const Hip &hip = Hip::get();
			uintptr_t destaddr = reinterpret_cast<uintptr_t>(msg.start);
			uint module = msg.module + 7;
			Hip::mem_iterator it;
			for(it = hip.mem_begin(); it != hip.mem_end(); ++it) {
				if(it->type == HipMem::MB_MODULE && module-- == 0)
					break;
			}
			if(it == hip.mem_end())
				return false;

			// does it fit in guest mem?
			if(destaddr >= _guest_mem.virt() + _guest_mem.size() ||
					destaddr + it->size > _guest_mem.virt() + _guest_mem.size()) {
				Serial::get().writef("Can't copy module %#Lx..%#Lx to %p (RAM is only 0..%p)\n",
						it->addr,it->addr + it->size,destaddr - _guest_mem.virt(),_guest_size);
				return false;
			}

			DataSpace ds(it->size,DataSpaceDesc::LOCKED,DataSpaceDesc::R,it->addr);
			memcpy(msg.start,reinterpret_cast<void*>(ds.virt()),ds.size());
			msg.size = it->size;
			msg.cmdlen = sizeof("kernel") - 1;
			msg.cmdline = "kernel";
			return true;
		}
		break;

		case MessageHostOp::OP_GET_MAC:
			assert(false);
			// TODO res = !Sigma0Base::hostop(msg);
			break;

		case MessageHostOp::OP_ATTACH_MSI:
		case MessageHostOp::OP_ATTACH_IRQ: {
			assert(false);
			/* TODO
			unsigned irq_cap = alloc_cap();
			myutcb()->head.crd = Crd(irq_cap,0,DESC_CAP_ALL).value();
			res = !Sigma0Base::hostop(msg);
			create_irq_thread(
					msg.type == MessageHostOp::OP_ATTACH_IRQ ? msg.value : msg.msi_gsi,irq_cap,
					do_gsi,"irq");
			*/
		}
		break;

		case MessageHostOp::OP_VCPU_CREATE_BACKEND: {
			VCPUBackend *v = new VCPUBackend(&_mb,msg.vcpu,Hip::get().has_svm(),CPU::current().log_id());
			msg.value = reinterpret_cast<ulong>(v);
			msg.vcpu->executor.add(this,receive_static<CpuMessage>);
		}
		break;

		case MessageHostOp::OP_VCPU_BLOCK: {
			VCPUBackend *v = reinterpret_cast<VCPUBackend*>(msg.value);
			globalsm.up();
			v->sm().down();
			globalsm.down();
			res = true;
		}
		break;

		case MessageHostOp::OP_VCPU_RELEASE: {
			VCPUBackend *v = reinterpret_cast<VCPUBackend*>(msg.value);
			if(msg.len)
				v->sm().up();
			v->vcpu().recall();
			res = true;
		}
		break;

		case MessageHostOp::OP_ALLOC_SEMAPHORE: {
			Sm *sm = new Sm(1);
			msg.value = sm->sel();
		}
		break;

		case MessageHostOp::OP_ALLOC_SERVICE_THREAD: {
			assert(false);
			/* TODO
			phy_cpu_no cpu = myutcb()->head.nul_cpunr;
			unsigned ec_cap = create_ec_helper(msg._alloc_service_thread.work_arg,cpu,_pt_irq,0,
					reinterpret_cast<void *>(msg._alloc_service_thread.work));
			AdmissionProtocol::sched sched(AdmissionProtocol::sched::TYPE_SPORADIC); //Qpd(2, 10000)
			return !service_admission->alloc_sc(*myutcb(),ec_cap,sched,cpu,"service");
			*/
		}
		break;

		case MessageHostOp::OP_CREATE_EC4PT:
			assert(false);
			/* TODO
			msg._create_ec4pt.ec = create_ec4pt(msg.obj,msg._create_ec4pt.cpu,
					Config::EXC_PORTALS * msg._create_ec4pt.cpu,msg._create_ec4pt.utcb_out,
					msg._create_ec4pt.ec);
			return msg._create_ec4pt.ec != 0;
			*/
			break;

		case MessageHostOp::OP_VIRT_TO_PHYS:
		case MessageHostOp::OP_REGISTER_SERVICE:
		case MessageHostOp::OP_ALLOC_SERVICE_PORTAL:
		case MessageHostOp::OP_WAIT_CHILD:
		default:
			Util::panic("%s - unimplemented operation %#x",__PRETTY_FUNCTION__,msg.type);
			break;
	}
	return res;
}

bool Vancouver::receive(MessagePciConfig &msg) {
	return false;// TODO !Sigma0Base::pcicfg(msg);
}

bool Vancouver::receive(MessageAcpi &msg) {
	return false;// TODO !Sigma0Base::acpi(msg);
}

bool Vancouver::receive(MessageTimer &msg) {
	COUNTER_INC("requestTO");
	switch(msg.type) {
		case MessageTimer::TIMER_NEW:
			msg.nr = _timeouts.alloc();
			return true;
		case MessageTimer::TIMER_REQUEST_TIMEOUT:
			_timeouts.request(msg.nr,msg.abstime);
			break;
		default:
			return false;
	}
	return true;
}

bool Vancouver::receive(MessageTime &msg) {
	_timeouts.time(msg.timestamp,msg.wallclocktime);
	return true;
}

bool Vancouver::receive(MessageLegacy &msg) {
	if(msg.type != MessageLegacy::RESET)
		return false;
	// TODO ??
	return true;
}

bool Vancouver::receive(MessageConsoleView &msg) {
	if(msg.type != MessageConsoleView::TYPE_GET_INFO)
		return false;
	msg.sess = &_conssess;
	return true;
}

void Vancouver::create_devices(const char *args) {
	_mb.bus_hostop.add(this,receive_static<MessageHostOp>);
	_mb.bus_consoleview.add(this,receive_static<MessageConsoleView>);
	// TODO _mb.bus_console.add(this,receive_static<MessageConsole>);
	// TODO _mb.bus_disk.add(this,receive_static<MessageDisk>);
	_mb.bus_timer.add(this,receive_static<MessageTimer>);
	_mb.bus_time.add(this,receive_static<MessageTime>);
	// TODO _mb.bus_network.add(this,receive_static<MessageNetwork>);
	_mb.bus_hwpcicfg.add(this,receive_static<MessageHwPciConfig>);
	_mb.bus_acpi.add(this,receive_static<MessageAcpi>);
	_mb.bus_legacy.add(this,receive_static<MessageLegacy>);
	_mb.parse_args(args);
}

void Vancouver::create_vcpus() {
	// init VCPUs
	for(VCpu *vcpu = _mb.last_vcpu; vcpu; vcpu = vcpu->get_last()) {
		// init CPU strings
		const char *short_name = "NOVA microHV";
		vcpu->set_cpuid(0,1,reinterpret_cast<const unsigned *>(short_name)[0]);
		vcpu->set_cpuid(0,3,reinterpret_cast<const unsigned *>(short_name)[1]);
		vcpu->set_cpuid(0,2,reinterpret_cast<const unsigned *>(short_name)[2]);
		const char *long_name = "Vancouver VMM proudly presents this VirtualCPU. ";
		for(unsigned i = 0; i < 12; i++)
			vcpu->set_cpuid(0x80000002 + (i / 4),i % 4,
					reinterpret_cast<const unsigned *>(long_name)[i]);

		// propagate feature flags from the host
		uint32_t ebx_1 = 0,ecx_1 = 0,edx_1 = 0;
		Util::cpuid(1,ebx_1,ecx_1,edx_1);
		vcpu->set_cpuid(1,1,ebx_1 & 0xff00,0xff00ff00); // clflush size
		vcpu->set_cpuid(1,2,ecx_1,0x00000201); // +SSE3,+SSSE3
		vcpu->set_cpuid(1,3,edx_1,0x0f80a9bf | (1 << 28)); // -PAE,-PSE36, -MTRR,+MMX,+SSE,+SSE2,+SEP
	}
}

int main(int argc,char *argv[]) {
	Vancouver *v = new Vancouver("PC_PS2",ExecEnv::PAGE_SIZE * 1024 * 8);
	v->reset();

	Sm sm(0);
	sm.down();
	return 0;
}
