#include "Routing.hpp"

#include "PacketParser.hpp"
#include "api/Packet.hpp"
#include <runos/core/logging.hpp>

#include <sstream>

namespace runos {
	REGISTER_APPLICATION(Routing, {"controller", "host-manager", "switch-manager", "topology", "link-discovery", ""})

	uint32_t stoui(std::string in) {
		std::string tmp = "";
		union Conv {
			uint32_t num;
			char adr[4];
		} adr;
		char *pos = adr.adr;
		for (char i : in) {
			if (i != '.') {
				tmp += i;
			} else {
				*pos = std::stoul(tmp);
				tmp = "";
				pos += 1;
			}
		}

		*pos = std::stoul(tmp);


		return adr.num;
	}


	void Routing::init(Loader* loader, const Config& config) {
		switch_manager_ = SwitchManager::get(loader);
		host_manager_ = HostManager::get(loader);
		link_discovery_ = dynamic_cast<LinkDiscovery*>(LinkDiscovery::get(loader));

		connect(switch_manager_, &SwitchManager::switchUp, this, &Routing::onSwitchUp);
		connect(switch_manager_, &SwitchManager::switchDown, this, &Routing::onSwitchDown);
		connect(host_manager_, &HostManager::hostDiscovered, this, &Routing::onHostDiscovered);
		connect(switch_manager_, &SwitchManager::linkUp, this, &Routing::onLinkUp);
		connect(switch_manager_, &SwitchManager::linkDown, this, &Routing::onLinkDown);
		connect(link_discovery_, &LinkDiscovery::linkDiscovered, this, &Routing::onLinkDiscovered);

		//handler
		handler_ = Controller::get(loader)->register_handler([=](of13::PacketIn& pi, OFConnectionPtr ofconn) mutable -> bool {
			PacketParser pp(pi);
			runos::Packet& pkt(pp);

			src_mac_ = pkt.load(ofb::eth_src);
			dst_mac_ = pkt.load(ofb::eth_dst);
			in_port_ = pkt.load(ofb::in_port);
			dpid_ = ofconn->dpid();
			eth_type_ = pkt.load(ofb::eth_type);
			
			if (eth_type_ == 0x0806) {
				arp_spa_ = pkt.load(ofb::arp_spa);
				arp_tpa_ = pkt.load(ofb::arp_tpa); 
				auto it = ip_switch_.find(arp_spa_);
				if (it == ip_switch_.end()) {
					LOG(INFO) << "NOT FOUND SPA";
					ip_switch_[arp_spa_] = dpid_;
					LOG(INFO) << ip_switch_[arp_spa_] << " " << arp_spa_ << in_port_;
					ip_port_[arp_spa_] = in_port_;
				} 
				it = ip_switch_.find(arp_tpa_);
				if (it == ip_switch_.end()) {
					Routing::sendBroadcast(pi);
				} else {
					LOG(INFO) << "TPA FOUND";
					uint32_t ds = it->second;
					if (dpid_ == ds) {
						auto it = ip_port_.find(arp_tpa_);
						uint32_t target = it->second;
						LOG(INFO) << target << " " << dpid_ << " " << ds;
						//Routing::sendUnicast(target, pi);
						Routing::sendBroadcast(pi);
					} else {
						uint32_t ns = next_hop_.getVol(dpid_, ds);
						if (ns == 0) {
							Routing::sendBroadcast(pi);
							return false;
						} else{
							LOG(INFO) << dpid_ << "^^^^" << ns;
							uint32_t target = ports_.getVol(dpid_, ns);
							//Routing::sendUnicast(target, pi);
							Routing::sendBroadcast(pi);
						}
					}
					//next hop [spa][tpa]? y - port = nh/ n - return false
					//send unicast
				}
			} else if (eth_type_ == 0x0800) {
				//LOG(INFO) << "IP";
				Routing::sendBroadcast(pi);
			}
			return true;
		}, -5);
	}

	void Routing::onSwitchUp(SwitchPtr sw) {
		weight_.newSwitch(sw->dpid());
		next_hop_.newSwitch(sw->dpid());
		ports_.newSwitch(sw->dpid());

		of13::FlowMod fm;
		fm.command(of13::OFPFC_ADD);
		fm.table_id(0);
		fm.priority(2);
		of13::ApplyActions applyActions;
		fm.add_oxm_field(new of13::EthType(0x0806));
		of13::OutputAction output_action(of13::OFPP_CONTROLLER, 0xFFFF);
		applyActions.add_action(output_action);
		fm.add_instruction(applyActions);
		sw->connection()->send(fm);

		of13::FlowMod fm2;
		fm2.command(of13::OFPFC_ADD);
		fm2.table_id(0);
		fm2.priority(1);
		of13::ApplyActions applyActions2;
		fm2.add_oxm_field(new of13::EthType(0x0800));
		of13::OutputAction output_action2(of13::OFPP_CONTROLLER, 0xFFFF);
		applyActions2.add_action(output_action2);
		fm2.add_instruction(applyActions2);
		sw->connection()->send(fm2);
	}

	void Routing::onLinkDiscovered(switch_and_port from, switch_and_port to) {
		weight_.newConnection(from.dpid, to.dpid);
		ports_.addVol(from.dpid, to.dpid, from.port);
		ports_.addVol(to.dpid, from.dpid, to.port);
		Routing::makeD();
	}
	void Routing::onHostDiscovered(Host* h) {
		LOG(INFO) << stoui(h->ip());
		ip_switch_[stoui(h->ip())] = h->switchID();
		ip_port_[stoui(h->ip())] = h->switchPort();
	}
	void Routing::onSwitchDown(SwitchPtr sw) {
		weight_.delSwitch(sw->dpid());
		ports_.delSwitch(sw->dpid());
		next_hop_.delSwitch(sw->dpid());
	}
	void Routing::onLinkUp(PortPtr PORT) {}
	void Routing::onLinkDown(PortPtr PORT) {}

	void Routing::sendUnicast (uint32_t target, const of13::PacketIn& pi) {
		{
		of13::PacketOut po;
		po.data(pi.data(), pi.data_len());
		of13::OutputAction output_action(target, of13::OFPCML_NO_BUFFER);
		LOG(INFO) << target << " " << dpid_;
		po.add_action(output_action);
		switch_manager_->switch_(dpid_)->connection()->send(po);
		}

		{
		of13::FlowMod fm;
		fm.command(of13::OFPFC_ADD);
		fm.table_id(0);
		fm.priority(2);
		std::stringstream ss;
		fm.idle_timeout(uint64_t(60));
		fm.hard_timeout(uint64_t(1800));
		of13::ApplyActions applyActions;
		of13::OutputAction output_action(target, of13::OFPCML_NO_BUFFER);
		applyActions.add_action(output_action);
		fm.add_instruction(applyActions);
		switch_manager_->switch_(dpid_)->connection()->send(fm);
		}
	}

	void Routing::sendBroadcast(const of13::PacketIn& pi) {
		of13::PacketOut po;
		po.data(pi.data(), pi.data_len());
		po.in_port(in_port_);
		of13::OutputAction output_action(of13::OFPP_ALL, of13::OFPCML_NO_BUFFER);
		po.add_action(output_action);
		switch_manager_->switch_(dpid_)->connection()->send(po);
	}

	void Routing::makeD() {
		weight_.printT();
		int size = weight_.size();
		uint32_t arr[size][size];
		std::unordered_map<uint32_t, uint32_t> ind;
		weight_.mtoa((uint32_t*) arr, ind);
		LOG(INFO) << size << "!!!";
		for (int i = 0; i < size; i++) {
			LOG(INFO) << "ARRAY " << i;
			for (int j = 0; j < size; j++) {
				LOG(INFO) << i << " " << j << " " << arr[i][j];
			}
		}
		for (auto i = ind.begin(); i != ind.end(); i++) {
			LOG(INFO) << i->first << " " << i->second;
		}
		for (int s = 0; s < size; s++) {
			uint32_t minD[size];
			uint32_t checkedV[size];
			uint32_t tmp, minimum;
			int minI;
			for(int i = 0; i < size; i++) {
				minD[i] = 10000;
				checkedV[i] = 1;
			}
			minD[s] = 0;
			do {
				minI = -1;
				minimum = 10000; 
				for(int i = 0; i < size; i++) {
					if((checkedV[i] == 1) && (minD[i] < minimum)) {
						minimum = minD[i];
						minI = i;
					}
				}
				if (minI != -1) {
					for (int i = 0; i < size; i++) {
						if (arr[minI][i] > 0) {
							tmp = minimum + arr[minI][i];
							if (tmp < minD[i]) {
								minD[i] = tmp;
							}
						}
					}
					checkedV[minI] = 0;
				}
			} while (minI != -1);
			for (uint32_t i = 0; i < size; i++) {
				uint32_t end = i;
				uint32_t ver[size];
				if (s != end) {
					auto it = ind.begin();
					LOG(INFO) << "!1";
					//for (int k = 0; k < end; k++) {
					//	it++;
					//}
					while (it->second != end) {
						it++;
					}
					ver[0] = it->first;
					int n = 1;
					uint32_t weight = minD[end];
					while (end != s) {
						//LOG(INFO) << "!2";
						for (int j = 0; j < size; j++) {
							LOG(INFO) << "!!!!!" << end << " " << s << " " << j << " " << arr[j][end];
							if (arr[j][end] != 0) {
								uint32_t tmp = weight - arr[j][end];
								if (tmp == minD[j]) {
									weight = tmp;
									end = j;
									LOG(INFO) << "!!!!!" << end << " " << s;
									auto it = ind.begin();
									while(it->second != end) {
										it++;
									}
									ver[n] = it->first;
									LOG(INFO) << "@@@" << ver[n];
									n++;
								}
							}
						}
						break;
					}
					for (int p = n-1; p >= 0; p--) {
						LOG(INFO) << s << " " << i << ": " << ver[p];
					}
					auto it1 = ind.begin();
					auto it2 = ind.begin();
					while (it1->second != s) {
						it1++;
					}
					while (it2->second != i) {
						it2++;
					}
					next_hop_.addVol(it1->first, it2->first, ver[n-1]);
				}
			}
			//LOG(INFO) << "s = " << s;
			//for (int i = 0; i < size; i++) {
			//	LOG(INFO) << minD[i];
			//}
		}

	}

} //namespace runos
