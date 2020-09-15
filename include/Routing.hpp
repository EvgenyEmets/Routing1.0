#pragma once

#include "Application.hpp"
#include "Loader.hpp"
#include "SwitchManager.hpp"
#include "Controller.hpp"
#include "HostManager.hpp"
#include "LinkDiscovery.hpp"
#include "api/SwitchFwd.hpp"
#include "oxm/openflow_basic.hh"

#include <boost/optional.hpp>
#include <boost/thread.hpp>

#include <unordered_map>

namespace runos {
	using SwitchPtr = safe::shared_ptr<Switch>;
	namespace of13 = fluid_msg::of13;

	namespace ofb {
		constexpr auto in_port = oxm::in_port();
		constexpr auto eth_src = oxm::eth_src();
		constexpr auto eth_dst = oxm::eth_dst();
		constexpr auto eth_type = oxm::eth_type();
		constexpr auto arp_spa = oxm::arp_spa();
		constexpr auto arp_tpa = oxm::arp_tpa();
		constexpr auto ip_src = oxm::ipv4_src();
		constexpr auto ip_dst = oxm::ipv4_dst();

	}
	
	class Tables {
		std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> table_;
	public:
		void newSwitch(uint32_t Sw) {
			Sw++;
			for(auto it = table_.begin(); it != table_.end(); it++) {
				it->second[Sw] = 0;
			}
			table_[Sw][Sw] = 0;
			for(auto it = table_.begin(); it != table_.end(); it++) {
				table_[Sw][it->first] = 0;
			}
		}
		void delSwitch(uint32_t Sw) {
			Sw++;
			for(auto it = table_.begin(); it != table_.end(); it++) {
				it->second.erase(Sw);
			}
			table_.erase(Sw);
		}
		void newConnection(uint32_t Sw1, uint32_t Sw2) {
			Sw1++;
			Sw2++;
			table_[Sw1][Sw2] = 1;
			table_[Sw2][Sw1] = 1;
		}
		void addVol(uint32_t Sw1, uint32_t Sw2, uint32_t vol) {
			Sw1++;
			Sw2++;
            table_[Sw1][Sw2] = vol;
        }
		int size() {
			return table_.size();
		}
		uint32_t getVol (const uint32_t i, const uint32_t j) {
			/*auto it = table_.begin();
			for (int k = 0; k < i; k++) {
				it++;
			}
			auto it2 = (it->second).begin();
			for (int k = 0; k < j; k++) {
				it2++;
			}*/
			return table_[i][j];
		}
		void printT () {
			auto it = table_.begin();
			for (it; it != table_.end(); it++) {
				LOG(INFO) << "switch:" << it->first;
				auto it2 = it->second.begin();
				for (it2; it2 != it->second.end(); it2++) {
					LOG(INFO) << it2->first << ": " << it2->second;
				}
			}
		}
		void mtoa (uint32_t *arr, std::unordered_map<uint32_t, uint32_t> &ind) {
			int size = table_.size();
			//uint32_t a[size][size];
			//LOG(INFO) << arr[0][0];
			int i = 0;
			for (auto it = table_.begin(); it != table_.end(); it++) {
				ind[it->first] = i++;
			}
			for (auto it = table_.begin(); it != table_.end(); it++) {
				for (auto it2 = it->second.begin(); it2 != it->second.end(); it2++) {
					arr[ind[it->first] * size + ind[it2->first]] = it2->second;
				}
			}
		}
	};

	class Routing : public Application {
		Q_OBJECT
		SIMPLE_APPLICATION(Routing, "Routing")
	public:
		void init(Loader* loader, const Config& config) override;

	protected slots:
		void onSwitchUp(SwitchPtr sw);
		void onSwitchDown(SwitchPtr sw);
		void onLinkDiscovered(switch_and_port from, switch_and_port to);
		void onLinkUp(PortPtr PORT);
		void onLinkDown(PortPtr PORT);
		void onHostDiscovered(Host* h);
		void makeD();
		void sendBroadcast(const of13::PacketIn& pi);
		void sendUnicast (uint32_t target, const of13::PacketIn& pi);
	private:
		OFMessageHandlerPtr handler_;
		SwitchManager* switch_manager_;
		HostManager* host_manager_;
		LinkDiscovery* link_discovery_;

		ethaddr src_mac_;
		ethaddr dst_mac_;
		uint32_t ip_src_;
		uint32_t ip_dst_;
		uint64_t dpid_;
		uint32_t in_port_;
		uint16_t eth_type_;
		uint32_t arp_spa_;
		uint32_t arp_tpa_;
		Tables weight_;
		Tables ports_;
		Tables next_hop_;
		std::unordered_map<uint32_t, uint32_t> ip_switch_;
		std::unordered_map<uint32_t, uint32_t> ip_port_;

		//methods

	};

} // namespace runos
