/**
 * Copyright (c) 2019 Paul-Louis Ageneau
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "rtc/rtc.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#define CUSTOM_MAX_MESSAGE_SIZE 1048576

using namespace rtc;
using namespace std;

template <class T> weak_ptr<T> make_weak_ptr(shared_ptr<T> ptr) { return ptr; }

void test_negotiated() {
	InitLogger(LogLevel::Debug);

	Configuration config1;
	config1.disableAutoNegotiation = true;
	PeerConnection pc1(config1);

	Configuration config2;
	config2.disableAutoNegotiation = true;
	PeerConnection pc2(config2);

	pc1.onLocalDescription([&pc2](Description sdp) {
		cout << "Description 1: " << sdp << endl;
		pc2.setRemoteDescription(string(sdp));
		pc2.setLocalDescription(); // Make the answer
	});

	pc1.onLocalCandidate([&pc2](Candidate candidate) {
		cout << "Candidate 1: " << candidate << endl;
		pc2.addRemoteCandidate(string(candidate));
	});

	pc2.onLocalDescription([&pc1](Description sdp) {
		cout << "Description 2: " << sdp << endl;
		pc1.setRemoteDescription(string(sdp));
	});

	pc2.onLocalCandidate([&pc1](Candidate candidate) {
		cout << "Candidate 2: " << candidate << endl;
		pc1.addRemoteCandidate(string(candidate));
	});

	// Try to open a negotiated channel
	DataChannelInit init;
	init.negotiated = true;
	init.id = 0; // ID 2 works.
	auto negotiated1 = pc1.createDataChannel("negotiated", init);
	auto negotiated2 = pc2.createDataChannel("negotiated", init);

	auto dc1 = pc1.createDataChannel("inband1");
	auto dc2 = pc2.createDataChannel("inband2");

	shared_ptr<DataChannel> dc1recv;
	pc1.onDataChannel([&dc1recv](shared_ptr<DataChannel> dc) {
		cout << "DataChannel 1: Received with label \"" << dc->label() << "\" id " << int(dc->id()) << endl;
		std::atomic_store(&dc1recv, dc);
	});
	shared_ptr<DataChannel> dc2recv;
	pc2.onDataChannel([&dc2recv](shared_ptr<DataChannel> dc) {
		cout << "DataChannel 2: Received with label \"" << dc->label() << "\" id " << int(dc->id()) << endl;
		std::atomic_store(&dc2recv, dc);
	});

	// Make the offer
	pc1.setLocalDescription();

	// Wait a bit
	int attempts = 10;
	while (!negotiated1->isOpen() || !negotiated2->isOpen() && attempts--)
		this_thread::sleep_for(1s);

	if (!negotiated1->isOpen() || !negotiated2->isOpen())
		throw runtime_error("Negotiated DataChannel is not open");

	std::atomic<bool> received = false;
	negotiated2->onMessage([&received](const variant<binary, string> &message) {
		if (holds_alternative<string>(message)) {
			cout << "Second Message 2: " << get<string>(message) << endl;
			received = true;
		}
	});

	negotiated1->send("Hello from negotiated channel");

	// Wait a bit
	attempts = 5;
	while (!received && attempts--)
		this_thread::sleep_for(1s);

	if (!received)
		throw runtime_error("Negotiated DataChannel failed");

	if (pc1.state() != PeerConnection::State::Connected &&
	    pc2.state() != PeerConnection::State::Connected)
		throw runtime_error("PeerConnection is not connected");

	attempts = 5;
	shared_ptr<DataChannel> adc2;
	while ((!(adc2 = std::atomic_load(&dc2recv)) || !adc2->isOpen() || !dc2->isOpen()) && attempts--)
		this_thread::sleep_for(1s);

	attempts = 5;
	shared_ptr<DataChannel> adc1;
	while ((!(adc1 = std::atomic_load(&dc1recv)) || !adc1->isOpen() || !dc1->isOpen()) && attempts--)
		this_thread::sleep_for(1s);

	if (!adc1 || !adc1->isOpen() || !dc1->isOpen())
		throw runtime_error("DataChannel 1 is not open");

	if (!adc2 || !adc2->isOpen() || !dc2->isOpen())
		throw runtime_error("DataChannel 2 is not open");



	if (auto addr = pc1.localAddress())
		cout << "Local address 1:  " << *addr << endl;
	if (auto addr = pc1.remoteAddress())
		cout << "Remote address 1: " << *addr << endl;
	if (auto addr = pc2.localAddress())
		cout << "Local address 2:  " << *addr << endl;
	if (auto addr = pc2.remoteAddress())
		cout << "Remote address 2: " << *addr << endl;

	Candidate local, remote;
	if (pc1.getSelectedCandidatePair(&local, &remote)) {
		cout << "Local candidate 1:  " << local << endl;
		cout << "Remote candidate 1: " << remote << endl;
	}
	if (pc2.getSelectedCandidatePair(&local, &remote)) {
		cout << "Local candidate 2:  " << local << endl;
		cout << "Remote candidate 2: " << remote << endl;
	}

	// Delay close of peer 2 to check closing works properly
	pc1.close();
	this_thread::sleep_for(1s);
	pc2.close();
	this_thread::sleep_for(1s);

	cout << "Success" << endl;
}
