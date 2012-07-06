/*
This source file is part of KBEngine
For the latest info, see http://www.kbengine.org/

Copyright (c) 2008-2012 KBEngine.

KBEngine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

KBEngine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
 
You should have received a copy of the GNU Lesser General Public License
along with KBEngine.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "dbmgr.hpp"
#include "dbmgr_interface.hpp"
#include "network/common.hpp"
#include "network/tcp_packet.hpp"
#include "network/udp_packet.hpp"
#include "network/message_handler.hpp"
#include "thread/threadpool.hpp"
#include "server/componentbridge.hpp"

#include "baseapp/baseapp_interface.hpp"
#include "cellapp/cellapp_interface.hpp"
#include "loginapp/loginapp_interface.hpp"

namespace KBEngine{
	
ServerConfig g_serverConfig;
KBE_SINGLETON_INIT(Dbmgr);

//-------------------------------------------------------------------------------------
Dbmgr::Dbmgr(Mercury::EventDispatcher& dispatcher, 
			 Mercury::NetworkInterface& ninterface, 
			 COMPONENT_TYPE componentType,
			 COMPONENT_ID componentID):
	ServerApp(dispatcher, ninterface, componentType, componentID),
	loopCheckTimerHandle_(),
	mainProcessTimer_(),
	idServer_(1, 1024)
{
}

//-------------------------------------------------------------------------------------
Dbmgr::~Dbmgr()
{
}

//-------------------------------------------------------------------------------------
bool Dbmgr::run()
{
	return ServerApp::run();
}

//-------------------------------------------------------------------------------------
void Dbmgr::handleTimeout(TimerHandle handle, void * arg)
{
	switch (reinterpret_cast<uintptr>(arg))
	{
		case TIMEOUT_TICK:
			this->handleMainTick();
			break;
		case TIMEOUT_CHECK_STATUS:
			this->handleCheckStatusTick();
			break;
		default:
			break;
	}
}

//-------------------------------------------------------------------------------------
void Dbmgr::handleMainTick()
{
	 //time_t t = ::time(NULL);
	 //DEBUG_MSG("Dbmgr::handleGameTick[%"PRTime"]:%u\n", t, time_);
	
	time_++;
	getNetworkInterface().handleChannels(&DbmgrInterface::messageHandlers);
}

//-------------------------------------------------------------------------------------
void Dbmgr::handleCheckStatusTick()
{
}

//-------------------------------------------------------------------------------------
bool Dbmgr::initializeBegin()
{
	return true;
}

//-------------------------------------------------------------------------------------
bool Dbmgr::inInitialize()
{
	return true;
}

//-------------------------------------------------------------------------------------
bool Dbmgr::initializeEnd()
{
	// 添加一个timer， 每秒检查一些状态
	loopCheckTimerHandle_ = this->getMainDispatcher().addTimer(1000000, this,
							reinterpret_cast<void *>(TIMEOUT_CHECK_STATUS));

	mainProcessTimer_ = this->getMainDispatcher().addTimer(1000000 / g_kbeSrvConfig.gameUpdateHertz(), this,
							reinterpret_cast<void *>(TIMEOUT_TICK));
	return true;
}

//-------------------------------------------------------------------------------------
void Dbmgr::finalise()
{
	loopCheckTimerHandle_.cancel();
	mainProcessTimer_.cancel();
}

//-------------------------------------------------------------------------------------
void Dbmgr::onReqAllocEntityID(Mercury::Channel* pChannel, int8 componentType, COMPONENT_ID componentID)
{
	KBEngine::COMPONENT_TYPE ct = static_cast<KBEngine::COMPONENT_TYPE>(componentType);

	// 获取一个id段 并传输给IDClient
	std::pair<ENTITY_ID, ENTITY_ID> idRange = idServer_.allocRange();
	Mercury::Bundle bundle(pChannel);

	if(ct == BASEAPP_TYPE)
		bundle.newMessage(BaseappInterface::onReqAllocEntityID);
	else	
		bundle.newMessage(CellappInterface::onReqAllocEntityID);

	bundle << idRange.first;
	bundle << idRange.second;
	bundle.send(this->getNetworkInterface(), pChannel);
}

//-------------------------------------------------------------------------------------
void Dbmgr::onRegisterNewApp(Mercury::Channel* pChannel, int32 uid, std::string& username, 
						int8 componentType, uint64 componentID, 
						uint32 intaddr, uint16 intport, uint32 extaddr, uint16 extport)
{
	ServerApp::onRegisterNewApp(pChannel, uid, username, componentType, componentID, 
						intaddr, intport, extaddr, extport);

	// 下一步:
	// 如果是连接到dbmgr则需要等待接收app初始信息
	// 例如：初始会分配entityID段以及这个app启动的顺序信息（是否第一个baseapp启动）
	if((KBEngine::COMPONENT_TYPE)componentType == BASEAPP_TYPE || 
		(KBEngine::COMPONENT_TYPE)componentType == CELLAPP_TYPE || 
		(KBEngine::COMPONENT_TYPE)componentType == LOGINAPP_TYPE)
	{
		Mercury::Bundle bundle;
		ENTITY_ID startID = 0;
		ENTITY_ID endID = 0;
		int32 startGlobalOrder = Componentbridge::getComponents().getGlobalOrderLog()[getUserUID()];
		int32 startGroupOrder = 0;

		switch((KBEngine::COMPONENT_TYPE)componentType)
		{
		case BASEAPP_TYPE:
			{
				startGroupOrder = Componentbridge::getComponents().getBaseappGroupOrderLog()[getUserUID()];

				std::pair<ENTITY_ID, ENTITY_ID> idRange = idServer_.allocRange();
				bundle.newMessage(BaseappInterface::onDbmgrInit);
				BaseappInterface::onDbmgrInitArgs4::staticAddToBundle(bundle, idRange.first, 
					idRange.second, startGlobalOrder, startGroupOrder);
			}
			break;
		case CELLAPP_TYPE:
			{
				startGroupOrder = Componentbridge::getComponents().getCellappGroupOrderLog()[getUserUID()];

				std::pair<ENTITY_ID, ENTITY_ID> idRange = idServer_.allocRange();
				bundle.newMessage(CellappInterface::onDbmgrInit);
				CellappInterface::onDbmgrInitArgs4::staticAddToBundle(bundle, idRange.first, 
					idRange.second, startGlobalOrder, startGroupOrder);
			}
			break;
		case LOGINAPP_TYPE:
			startGroupOrder = Componentbridge::getComponents().getLoginappGroupOrderLog()[getUserUID()];

			bundle.newMessage(LoginappInterface::onDbmgrInit);
			LoginappInterface::onDbmgrInitArgs2::staticAddToBundle(bundle, startGlobalOrder, startGroupOrder);
			break;
		default:
			break;
		}

		bundle.send(networkInterface_, pChannel);
	}
}

//-------------------------------------------------------------------------------------

}
