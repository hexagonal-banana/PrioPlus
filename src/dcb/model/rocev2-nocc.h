/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: F.Y. Xue <xue.fyang@foxmail.com>
 */

#ifndef ROCEV2_NOCC_H
#define ROCEV2_NOCC_H

#include "rocev2-congestion-ops.h"

namespace ns3
{

class RoCEv2SocketState;

/**
 * This class implements a congestion control algorithm that controls nothing.
 */
class RoCEv2Nocc : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    RoCEv2Nocc();

    RoCEv2Nocc(Ptr<RoCEv2SocketState> sockState);

    std::string GetName() const override;

    inline std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const
    {
        // This has no Stats now
        return nullptr;
    }

    void Init();

    virtual void SetReady() override;
    
    private:
    std::shared_ptr<Stats> m_stats;
    bool m_fairShare;
    double m_targetInflight;

}; // class RoCEv2Nocc

} // namespace ns3

#endif // NOCC_H