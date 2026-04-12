import os
import time
import json
import struct
import socket
import operator
from functools import wraps
from typing import Annotated, TypedDict, Union
from dotenv import load_dotenv

from langchain_core.tools import tool
from langgraph.prebuilt import ToolNode
from langchain_openai import ChatOpenAI
from langgraph.graph import StateGraph, END
from langchain_core.messages import HumanMessage, SystemMessage

class ExchangeClient:
    def __init__(self, host='127.0.0.1', port=12345):
        self.host = host
        self.port = port
        self.sock = None
    
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print(f"--- [Connected to C++ Server at {self.host}:{self.port}] ---")
    
    def send_request(self, action: str, **kwargs):
        payload = {"action": action}
        payload.update(kwargs)
        json_data = json.dumps(payload).encode('utf-8')
        header = struct.pack('!I', len(json_data))
        self.sock.sendall(header + json_data)
    
    def receive_response(self):
        header_data = self.sock.recv(4)
        if not header_data: return None
        msg_len = struct.unpack('!I', header_data)[0]
        body_data = b""
        while len(body_data) < msg_len:
            chunk = self.sock.recv(msg_len - len(body_data))
            if not chunk: break
            body_data += chunk
        return json.loads(body_data.decode('utf-8'))
    
    def close(self):
        if self.sock: self.sock.close()

client = ExchangeClient()
client.connect()
load_dotenv()

# ==========================================
# 2. 风控拦截器 (修复版)
# ==========================================
def risk_control(func):
    @wraps(func) # 必须加 wraps，否则 LangChain 无法识别工具的参数签名
    def wrapper(*args, **kwargs):
        amount = kwargs.get("amount", 0)
        price = kwargs.get("price", 0)
        if amount * price > 100000:
            print(f"\n[风控系统拦截] 尝试交易金额 {amount * price} > 100000，已拒绝！")
            return {"status": "error", "msg": "风控拦截：单笔交易金额过大(>100000)"}
        return func(*args, **kwargs)
    return wrapper

# ==========================================
# 3. Tools 定义
# ==========================================
@tool
def register_account(username: str, password: str):
    """在交易所注册一个新的账号"""
    client.send_request("register", username=username, password=password)
    return client.receive_response()

@tool
def login_account(username: str, password: str):
    """登录交易所账号"""
    client.send_request("login", username=username, password=password)
    return client.receive_response()

@tool
def logout_account():
    """退出当前登录的账号。"""
    client.send_request("exit")
    return client.receive_response()

@tool
def query_balance():
    """查询当前登录账号的账户余额（现金）。"""
    client.send_request("getbalance")
    return client.receive_response()

@tool
def deposit_funds(amount: float):
    """向账户充值。当余额不足无法买入时，需调用此工具。"""
    client.send_request("deposit", amount=amount)
    return client.receive_response()

@tool
def query_my_holdings():
    """查询当前登录账号持有的所有股票及其数量。"""
    client.send_request("getholdings")
    return client.receive_response()

@tool
def query_market_depth(symbol: str):
    """查看指定股票的当前盘口深度（买一价 bid, 卖一价 ask）。"""
    client.send_request("getmarket", symbol=symbol)
    return client.receive_response()

@tool
def get_market_news():
    """获取宏观市场新闻，用于判断市场情绪。"""
    client.send_request("getnews")
    return client.receive_response()

@tool
def get_price_trend(symbol: str):
    """获取指定股票的价格波动趋势（最近5个时间点的涨跌百分比）。"""
    client.send_request("gettrend", symbol=symbol)
    return client.receive_response()

@tool
def get_all_stocks():
    """获取当前交易所支持交易的所有股票代码(symbol)列表。"""
    client.send_request("getallstocks")
    return client.receive_response()

@tool
@risk_control # 加入风控
def place_buy_order(symbol: str, price: float, amount: int):
    """提交买入订单。提交前请确保余额充足。"""
    client.send_request("buy", symbol=symbol, price=price, amount=amount)
    return client.receive_response()

@tool
@risk_control # 加入风控
def place_sell_order(symbol: str, price: float, amount: int):
    """提交卖出订单。提交前请确认持有该股票。"""
    client.send_request("sell", symbol=symbol, price=price, amount=amount)
    return client.receive_response()

@tool
def get_torders():
    """查看当前账号所有未成交的活动挂单"""
    client.send_request("getorders")
    return client.receive_response()

@tool
def cancel_order(symbol: str, type: str, order_id: str):
    """撤销尚未成交的挂单。撤单成功后，冻结的资产将原路退回。"""
    client.send_request("cancelorder", symbol=symbol, type=type, order_id=order_id)
    return client.receive_response()

tools =[
    register_account, login_account, logout_account,
    query_balance, deposit_funds, query_my_holdings,
    query_market_depth, get_market_news, get_price_trend,
    place_buy_order, place_sell_order, get_torders,
    cancel_order, get_all_stocks
]
tool_node = ToolNode(tools)

# ==========================================
# 4. LangGraph 状态机定义
# ==========================================
class AgentState(TypedDict):
    messages: Annotated[list, operator.add]

llm = ChatOpenAI(
    base_url=os.getenv("DEEPSEEK_BASE_URL", "https://api.deepseek.com"),
    api_key=os.getenv("DEEPSEEK_API_KEY"), 
    model="deepseek-chat",
    temperature=0.1 # 降低温度，保证交易逻辑严谨
).bind_tools(tools)

def call_model(state: AgentState):
    """大脑思考节点"""
    system_prompt = """你是一个运行在 OODA-V 架构下的全自动量化交易 Agent。
    你处于一个无限的时间循环(Tick)中。每次对话，我都会给你最新的【真实世界快照】。
    
    【你的核心纪律】：
    1. 绝不产生幻觉：订单是否成交，唯一的判断标准是它是否从【活动挂单】中消失，且【可用持仓/余额】发生变化。
    2. 状态认知：你下达买卖单后，资产会被冻结，并出现在【活动挂单】中。不要以为下单了就立刻成交了！
    3. 撤单逻辑：如果你发现某个订单在【活动挂单】里挂了很久，或者盘口价格已经偏离，你必须调用 cancel_order 撤单。
    4. 你的长期任务：将持仓的股票挂上一个合适的价格，如果该股票没有过成交记录就以200一股的价格卖出。
    5. 挂单后会冻结相应的资产，查询资产时将无法查到。
    
    请根据最新的快照，决定本轮 Tick 的操作。如果不需要操作，请回复“保持观察”。
    """
    response = llm.invoke([SystemMessage(content=system_prompt)] + state["messages"])
    return {"messages": [response]}

def should_continue(state: AgentState):
    """路由节点"""
    last_message = state["messages"][-1]
    if last_message.tool_calls:
        return "continue"
    return "end"

workflow = StateGraph(AgentState)
workflow.add_node("agent", call_model)
workflow.add_node("action", tool_node)
workflow.set_entry_point("agent")
workflow.add_conditional_edges("agent", should_continue, {"continue": "action", "end": END})
workflow.add_edge("action", "agent")
app = workflow.compile()

# ==========================================
# 5. OODA-V 主循环控制器
# ==========================================
class OODAV_Runner:
    def __init__(self, agent_app, cpp_client):
        self.app = agent_app
        self.client = cpp_client
        self.tick_count = 0

    def start_loop(self, interval=5):
        print("\n=== [系统初始化] 正在自动登录... ===")
        self.client.send_request("login", username="user", password="123")
        print("登录结果:", self.client.receive_response())

        print("\n启动 OODA-V 交易 Agent 主循环...")
        while True:
            self.tick_count += 1
            print(f"\n{'='*20} [Tick {self.tick_count} | {time.strftime('%H:%M:%S')}] {'='*20}")
            
            try:
                # ---------------------------------------------------------
                # 1. Observe (观察) & Orient (对齐)
                # ---------------------------------------------------------
                # 强制从 C++ 拉取最新状态，剥夺大模型瞎猜的权利
                self.client.send_request("getbalance")
                balance = self.client.receive_response()
                
                self.client.send_request("getholdings")
                holdings = self.client.receive_response()
                
                self.client.send_request("getorders")
                orders = self.client.receive_response()

                # 组装系统快照
                system_snapshot = f"""
                【当前 Tick】: {self.tick_count}
                【账户余额】: {balance}
                【当前可用持仓】: {holdings}
                【当前活动挂单】: {orders}
                """
                print(f"[Observe] 成功获取世界快照。当前挂单数: {len(orders.get('data',[])) if isinstance(orders, dict) else '未知'}")

                # ---------------------------------------------------------
                # 2. Decide (决策) & Act (行动)
                # ---------------------------------------------------------
                # 注意：每次 Tick 都是全新的 inputs，防止上下文爆炸！
                user_prompt = f"这是当前最新的环境快照：\n{system_snapshot}\n请执行你的交易策略。"
                inputs = {"messages": [HumanMessage(content=user_prompt)]}

                print("[Decide] Agent 正在思考...")
                for output in self.app.stream(inputs):
                    for key, value in output.items():
                        if key == "action":
                            for msg in value["messages"]:
                                print(f"  >> [Act] C++ 返回数据: {msg.content}")
                        elif key == "agent":
                            content = value['messages'][-1].content
                            if content:
                                print(f"  >> [Agent 回复]: {content}")

                # ---------------------------------------------------------
                # 3. Verify (验证)
                # ---------------------------------------------------------
                # 验证逻辑已经融入到下一轮 Tick 的 Observe 中（挂单是否还在 orders 里）
                
            except Exception as e:
                print(f"[系统异常] Tick {self.tick_count} 发生错误: {e}")
            
            # 休眠等待下一个心跳
            time.sleep(interval)

# ==========================================
# 6. 运行入口
# ==========================================
if __name__ == "__main__":
    # 实例化并启动 OODA-V 循环
    runner = OODAV_Runner(agent_app=app, cpp_client=client)
    
    try:
        # 每 10 秒执行一次循环 (可根据 C++ 服务器压力调整)
        runner.start_loop(interval=10) 
    except KeyboardInterrupt:
        print("\n--- [接收到退出指令，Agent 停止工作] ---")
        client.close()