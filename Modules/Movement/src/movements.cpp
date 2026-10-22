#include "Movement/Movements.h"

// namespace X_Y {
//		//void MovementDispatcher::Connect(
//		//	MovementSender sender,
//		//	MovementType type,
//		//	MovementReceiver receiver,
//		//	MovementHandler handler
//		//) {
//		//	MovementBinding binding;
//		//	binding.sender = sender;
//		//	binding.type = type;
//		//	binding.receiver = receiver;
//		//	binding.handler = std::move(handler);
//		//	m_Bindings.push_back(binding);
//		//	XDEBUG("加入回调函数，队列有{}个", m_Bindings.size())
//		//}
//		//void MovementDispatcher::disConnect(
//		//	MovementSender sender,
//		//	MovementType type,
//		//	MovementReceiver receiver
//		//) {
//		//	// 遍历 → 找到匹配的三项 → 删除
//		//	for (auto it = m_Bindings.begin(); it != m_Bindings.end();) {
//		//		if (it->sender == sender &&
//		//			it->type == type &&
//		//			it->receiver == receiver)
//		//		{
//		//			it = m_Bindings.erase(it); // 删除并迭代
//		//		}
//		//		else {
//		//			++it;
//		//		}
//		//	}
//		//}
//		void MovementDispatcher::disConnect(MovementSender sender,
//			MovementReceiver receiver) {
//			for (auto it = m_Bindings.begin(); it != m_Bindings.end();) {
//				if (it->receiver == receiver && it->sender == sender)
//				{
//					XDEBUG("回调被删除")
//						it = m_Bindings.erase(it); // 删除并迭代
//				}
//				else {
//					++it;
//				}
//			}
//		}
//		void MovementDispatcher::disConnect(MovementReceiver receiver) {
//			for (auto it = m_Bindings.begin(); it != m_Bindings.end();) {
//				if (it->receiver == receiver)
//				{
//					XDEBUG("回调被删除")
//						it = m_Bindings.erase(it); // 删除并迭代
//				}
//				else {
//					++it;
//				}
//			}
//		}
//		// === 触发：发送行为，自动找到所有绑定并执行 ===
//		bool MovementDispatcher::DispatchEvent(Movement* event)
//		{
//			for (auto& binding : m_Bindings)
//			{
//				if (binding.sender == event->sender &&
//					binding.typeId == event->GetType())
//				{
//					binding.handler(*event);
//				}
//			}
//			event->Handled = true;
//			return event->Handled;
//		}
// }