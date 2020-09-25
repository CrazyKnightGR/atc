// 
// This file is part of AT&C project which simulates virtual world of air traffic and ATC.
// Code licensing terms are available at https://github.com/felix-b/atc/blob/master/LICENSE
// 
#include <algorithm>
#include "libworld.h"

using namespace std;

namespace world
{
    void World::progressTo(chrono::microseconds futureTimestamp)
    {
        const char *lastStep = "enter";
        try
        {
            auto delta = futureTimestamp - m_timestamp; 
            m_lastTimestampDelta = futureTimestamp - m_timestamp;
            m_timestamp = futureTimestamp;

            processDueWorkItems();
            lastStep = "processDueWorkItems";

            processFlights();
            lastStep = "processFlights";

            processControlFacilities();
            lastStep = "processControlFacilities";

            processHeartbeat();
            lastStep = "processHeartbeat";
        }
        catch(const std::exception& e)
        {
            m_host->writeLog("World::progressTo(%lld)/lastStep[%s] CRASHED!!! %s", futureTimestamp.count(), lastStep, e.what());
            throw;
        }
    }

    void World::addFlight(shared_ptr<Flight> flight)
    {
        m_flights.push_back(flight);
        m_flightById.insert({ flight->id(), flight });

        flight->onChanges([this]() {
            return m_changeSet;
        });

        m_changeSet->m_flights.added(flight);

        auto flightPlan = flight->plan();
        auto aircraft = flight->aircraft();
        
        m_host->writeLog(
            "Added flight: %s(%s->%s) aircraft id=%d model=%s",
            flight->callSign().c_str(), 
            flightPlan->departureAirportIcao().c_str(),
            flightPlan->arrivalAirportIcao().c_str(),
            aircraft->id(),
            aircraft->modelIcao().c_str());
    }

    void World::addFlightColdAndDark(shared_ptr<Flight> flight)
    {
        addFlight(flight);

        auto airport = getValueOrThrow(m_airportByIcao, flight->plan()->departureAirportIcao());
        auto parkingStand = airport->getParkingStandOrThrow(flight->plan()->departureGate());
        flight->aircraft()->park(parkingStand);
    }

    void World::processDueWorkItems()
    {
        if (m_workItemQueue.empty() || m_workItemQueue.top().timestamp > m_timestamp)
        {
            return;
        }

        m_host->writeLog("World is processing due work items");
        int count = 0;

        while (!m_workItemQueue.empty() && m_workItemQueue.top().timestamp <= m_timestamp)
        {
            try
            {
                m_workItemQueue.top().callback();
            }
            catch(const exception& e)
            {
                m_host->writeLog("World::processDueWorkItems(): a callback FAILED! %s", e.what());
            }
            
            m_workItemQueue.pop();
            count++;
        }

        m_host->writeLog("World has processed %d work items, %d remaining", count, m_workItemQueue.size());
    }

    void World::processFlights()
    {
        for (const auto& flight : m_flights)
        {
            flight->progressTo(m_timestamp);
        }
    }

    void World::processControlFacilities()
    {
        for (const auto& facility : m_controlFacilities)
        {
            facility->progressTo(m_timestamp);
        }
    }

    void World::processHeartbeat()
    {
        if ((m_timestamp - m_lastHearbeatTimestamp).count() >= 1000000)
        {
            m_heartbeatCount++;
            m_lastHearbeatTimestamp = m_timestamp;
            m_host->writeLog("Heartbeat # %llu", m_heartbeatCount);
        }
    }

    shared_ptr<World::ChangeSet> World::takeChanges()
    {
        auto temp = m_changeSet;
        m_changeSet = make_shared<World::ChangeSet>();
        return temp;
    }

    void World::deferUntilNextTick(function<void()> callback)
    {
        m_workItemQueue.push({ m_timestamp, callback });
    }
    
    void World::deferUntil(time_t time, function<void()> callback)
    {
        time_t deltaTimeInSeconds = time - currentTime();
        chrono::microseconds deferredTimestamp = chrono::microseconds(m_timestamp.count() + deltaTimeInSeconds * 1000000);
        m_workItemQueue.push({ deferredTimestamp, callback });
    }
    
    void World::deferBy(chrono::microseconds microseconds, function<void()> callback)
    {
        m_workItemQueue.push({ m_timestamp + microseconds, callback });
    }

    shared_ptr<Frequency> World::tryFindCommFrequency(shared_ptr<Flight> flight, int frequencyKhz)
    {
        //TODO: generalize for airborne flights

        //currently assuming the flight is at departure airport
        auto airport = getAirport(flight->plan()->departureAirportIcao());
        
        for (const auto& position : airport->tower()->positions())
        {
            if (position->frequency()->khz() == frequencyKhz)
            {
                return position->frequency();
            }
        }
        
        return nullptr;
    }

    const Runway::End& World::getRunwayEnd(const string& airportIcao, const string& runwayName) const
    {
        auto airport = getAirport(airportIcao);
        auto runway = airport->getRunwayOrThrow(runwayName);
        return runway->getEndOrThrow(runwayName);
    }


    bool World::compareWorkItems(const World::WorkItem& left, const World::WorkItem& right)
    {
        return (left.timestamp > right.timestamp);
    }
}
