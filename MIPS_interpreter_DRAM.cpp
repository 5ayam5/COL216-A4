// COL216 Assignment 4
// Mallika Prabhakar 2019CS50440
// Sayam Sethi 2019CS10399

// @TODO forwarding

#include <bits/stdc++.h>
#include <boost/tokenizer.hpp>

using namespace std;

// struct to store the registers and the functions to be executed
struct MIPS_Architecture
{
private:
	// struct to store all information required by DRAM queue to execute the instruction
	struct QElem
	{
		bool id;
		int PCaddr, value, issueCycle, startCycle, remainingCycles;

		/**
		 * initialise the queue element
		 * @param id 0 for sw and 1 for lw
		 * @param PCaddr stores the PC address of corresponding instruction
		 * @param value content to be stored (for sw) / register id (for lw)
		 * @param issueCycle the cycle in which the request was issued
		 * @param startCycle the cycle when the instruction began
		 * @param remainingCycles number of cycles pending to finish execution of request
		*/
		QElem(bool id, int PCaddr, int value, int issueCycle, int startCycle = -1, int remainingCycles = -1)
		{
			this->id = id;
			this->PCaddr = PCaddr;
			this->value = value;
			this->issueCycle = issueCycle;
			this->startCycle = startCycle;
			this->remainingCycles = remainingCycles;
		}
	};

public:
	// constants
	static const int MAX = (1 << 20), ROWS = (1 << 10);
	// DRAM delays
	int row_access_delay, col_access_delay;
	// instruction set
	unordered_map<string, function<int(MIPS_Architecture &, string, string, string)>> instructions;
	// mapping names to a unique number
	unordered_map<string, int> registerMap, address;
	// vector to store the commands in input program
	vector<vector<string>> commands;
	// "dynamic" vars
	int registers[32], PCcurr, PCnext, delay, currRow, currCol, clockCycles, rowBufferUpdates;
	pair<int, int> registersAddrDRAM[32];
	// data stored in allocated memory
	vector<vector<int>> data;
	// keep track of number of times each command was executed
	vector<int> commandCount;
	// data structure to store info about requests sent to the DRAM, key is the row number and value is QElem
	unordered_map<int, unordered_map<int, queue<QElem>>> DRAM_Buffer;
	// last location accessed by DRAM is stored
	pair<int, int> lastAddr;

	// constructor to initialise the instruction set
	MIPS_Architecture(ifstream &file, int row_delay, int col_delay)
	{
		row_access_delay = row_delay, col_access_delay = col_delay;

		instructions = {{"add", &MIPS_Architecture::add}, {"sub", &MIPS_Architecture::sub}, {"mul", &MIPS_Architecture::mul}, {"beq", &MIPS_Architecture::beq}, {"bne", &MIPS_Architecture::bne}, {"slt", &MIPS_Architecture::slt}, {"j", &MIPS_Architecture::j}, {"lw", &MIPS_Architecture::lw}, {"sw", &MIPS_Architecture::sw}, {"addi", &MIPS_Architecture::addi}};

		for (int i = 0; i < 32; ++i)
			registerMap["$" + to_string(i)] = i;
		registerMap["$zero"] = 0;
		registerMap["$at"] = 1;
		registerMap["$v0"] = 2;
		registerMap["$v1"] = 3;
		for (int i = 0; i < 4; ++i)
			registerMap["$a" + to_string(i)] = i + 4;
		for (int i = 0; i < 8; ++i)
			registerMap["$t" + to_string(i)] = i + 8, registerMap["$s" + to_string(i)] = i + 16;
		registerMap["$t8"] = 24;
		registerMap["$t9"] = 25;
		registerMap["$k0"] = 26;
		registerMap["$k1"] = 27;
		registerMap["$gp"] = 28;
		registerMap["$sp"] = 29;
		registerMap["$s8"] = 30;
		registerMap["$ra"] = 31;

		constructCommands(file);
	}

	// perform add immediate operation
	int addi(string r1, string r2, string num)
	{
		if (!checkRegisters({r1, r2}) || registerMap[r1] == 0)
			return 1;
		try
		{
			if (registersAddrDRAM[registerMap[r1]] != make_pair(-1, -1))
			{
				if (DRAM_Buffer[currRow][currCol].front().issueCycle == registersAddrDRAM[registerMap[r1]].first)
					return -registerMap[r1] - 1;
				registersAddrDRAM[registerMap[r1]] = {-1, -1};
			}
			if (registersAddrDRAM[registerMap[r2]] != make_pair(-1, -1))
				return -registerMap[r2] - 1;
			registers[registerMap[r1]] = registers[registerMap[r2]] + stoi(num);
			PCnext = PCcurr + 1;
			return 0;
		}
		catch (exception &e)
		{
			return 4;
		}
	}

	// perform add operation
	int add(string r1, string r2, string r3)
	{
		return op(r1, r2, r3, [&](int a, int b) { return a + b; });
	}

	// perform subtraction operation
	int sub(string r1, string r2, string r3)
	{
		return op(r1, r2, r3, [&](int a, int b) { return a - b; });
	}

	// perform multiplication operation
	int mul(string r1, string r2, string r3)
	{
		return op(r1, r2, r3, [&](int a, int b) { return a * b; });
	}

	// implements slt operation
	int slt(string r1, string r2, string r3)
	{
		return op(r1, r2, r3, [&](int a, int b) { return a < b; });
	}

	// perform the operation
	int op(string r1, string r2, string r3, function<int(int, int)> operation)
	{
		if (!checkRegisters({r1, r2, r3}) || registerMap[r1] == 0)
			return 1;
		if (registersAddrDRAM[registerMap[r1]] != make_pair(-1, -1))
		{
			if (DRAM_Buffer[currRow][currCol].front().issueCycle == registersAddrDRAM[registerMap[r1]].first)
				return -registerMap[r1] - 1;
			registersAddrDRAM[registerMap[r1]] = {-1, -1};
		}
		if (registersAddrDRAM[registerMap[r2]] != make_pair(-1, -1))
			return -registerMap[r2] - 1;
		if (registersAddrDRAM[registerMap[r3]] != make_pair(-1, -1))
			return -registerMap[r3] - 1;
		registers[registerMap[r1]] = operation(registers[registerMap[r2]], registers[registerMap[r3]]);
		PCnext = PCcurr + 1;
		return 0;
	}

	// perform the beq operation
	int beq(string r1, string r2, string label)
	{
		return bOP(r1, r2, label, [](int a, int b) { return a == b; });
	}

	// perform the bne operation
	int bne(string r1, string r2, string label)
	{
		return bOP(r1, r2, label, [](int a, int b) { return a != b; });
	}

	// implements beq and bne by taking the comparator
	int bOP(string r1, string r2, string label, function<bool(int, int)> comp)
	{
		if (!checkLabel(label))
			return 4;
		if (address.find(label) == address.end() || address[label] == -1)
			return 2;
		if (!checkRegisters({r1, r2}))
			return 1;
		if (registersAddrDRAM[registerMap[r1]] != make_pair(-1, -1))
			return -registerMap[r1] - 1;
		if (registersAddrDRAM[registerMap[r2]] != make_pair(-1, -1))
			return -registerMap[r2] - 1;
		PCnext = comp(registers[registerMap[r1]], registers[registerMap[r2]]) ? address[label] : PCcurr + 1;
		return 0;
	}

	// perform the jump operation
	int j(string label, string unused1 = "", string unused2 = "")
	{
		if (!checkLabel(label))
			return 4;
		if (address.find(label) == address.end() || address[label] == -1)
			return 2;
		PCnext = address[label];
		return 0;
	}

	// perform load word operation
	int lw(string r, string location, string unused1 = "")
	{
		if (!checkRegister(r) || registerMap[r] == 0)
			return 1;
		auto address = locateAddress(location);
		if (!address.first)
			return address.second;
		DRAM_Buffer[address.second / ROWS][(address.second % ROWS) / 4].push({1, PCcurr, registerMap[r], clockCycles + 1});
		registersAddrDRAM[registerMap[r]] = {clockCycles + 1, address.second};
		PCnext = PCcurr + 1;
		return 0;
	}

	// perform store word operation
	int sw(string r, string location, string unused1 = "")
	{
		if (!checkRegister(r))
			return 1;
		auto address = locateAddress(location);
		if (!address.first)
			return address.second;
		if (registersAddrDRAM[registerMap[r]] != make_pair(-1, -1))
			return -registerMap[r] - 1;
		DRAM_Buffer[address.second / ROWS][(address.second % ROWS) / 4].push({0, PCcurr, registers[registerMap[r]], clockCycles + 1});
		++rowBufferUpdates;
		PCnext = PCcurr + 1;
		return 0;
	}

	// parse the address and return the actual address or error
	pair<bool, int> locateAddress(string location)
	{
		if (location.back() == ')')
		{
			try
			{
				int lparen = location.find('('), offset = stoi(lparen == 0 ? "0" : location.substr(0, lparen));
				string reg = location.substr(lparen + 1);
				reg.pop_back();
				if (!checkRegister(reg))
					return {false, 3};
				if (registersAddrDRAM[registerMap[reg]] != make_pair(-1, -1))
					return {false, -registerMap[reg] - 1};
				int address = registers[registerMap[reg]] + offset;
				if (address % 4 || address < int(4 * commands.size()) || address >= MAX)
					return {false, 3};
				return {true, address};
			}
			catch (exception &e)
			{
				return {false, 4};
			}
		}
		try
		{
			int address = stoi(location);
			if (address % 4 || address < int(4 * commands.size()) || address >= MAX)
				return {false, 3};
			return {true, address};
		}
		catch (exception &e)
		{
			return {false, 4};
		}
	}

	// checks if label is valid
	inline bool checkLabel(string str)
	{
		return str.size() > 0 && isalpha(str[0]) && all_of(++str.begin(), str.end(), [](char c) { return (bool)isalnum(c); }) && instructions.find(str) == instructions.end();
	}

	// checks if the register is a valid one
	inline bool checkRegister(string r)
	{
		return registerMap.find(r) != registerMap.end();
	}

	// checks if all of the registers are valid or not
	bool checkRegisters(vector<string> regs)
	{
		return all_of(regs.begin(), regs.end(), [&](string r) { return checkRegister(r); });
	}

	/*
		handle all exit codes:
		0: correct execution
		1: register provided is incorrect
		2: invalid label
		3: unaligned or invalid address
		4: syntax error
		5: commands exceed memory limit
	*/
	void handleExit(int code)
	{
		switch (code)
		{
		case 1:
			cerr << "Invalid register provided or syntax error in providing register\n";
			break;
		case 2:
			cerr << "Label used not defined or defined too many times\n";
			break;
		case 3:
			cerr << "Unaligned or invalid memory address specified\n";
			break;
		case 4:
			cerr << "Syntax error encountered\n";
			break;
		case 5:
			cerr << "Memory limit exceeded\n";
			break;
		default:
			break;
		}
		if (code != 0)
		{
			cerr << "Error encountered at:\n";
			for (auto &s : commands[PCcurr])
				cerr << s << ' ';
			cerr << '\n';
		}

		cout << "Exit code: " << code << '\n';

		cout << "\nThe Row Buffer was updated " << rowBufferUpdates << " times.\n";
		cout << "\nFollowing are the non-zero data values:\n";
		for (int i = 0; i < ROWS; ++i)
			for (int j = 0; j < ROWS / 4; ++j)
				if (data[i][j] != 0)
					cout << (ROWS * i + 4 * j) << '-' << (ROWS * i + 4 * j) + 3 << hex << ": " << data[i][j] << '\n'
						 << dec;
		cout << "\nTotal number of cycles: " << clockCycles << " + " << delay << " (cycles taken for code execution + final writeback delay)\n";
		cout << "\nCount of instructions executed:\n";
		for (int i = 0; i < (int)commands.size(); ++i)
		{
			cout << commandCount[i] << " times:\t";
			for (auto &s : commands[i])
				cout << s << ' ';
			cout << '\n';
		}
	}

	// parse the command assuming correctly formatted MIPS instruction (or label)
	void parseCommand(string line)
	{
		// strip until before the comment begins
		line = line.substr(0, line.find('#'));
		vector<string> command;
		boost::tokenizer<boost::char_separator<char>> tokens(line, boost::char_separator<char>(", \t"));
		for (auto &s : tokens)
			command.push_back(s);
		// empty line or a comment only line
		if (command.empty())
			return;
		else if (command.size() == 1)
		{
			string label = command[0].back() == ':' ? command[0].substr(0, command[0].size() - 1) : "?";
			if (address.find(label) == address.end())
				address[label] = commands.size();
			else
				address[label] = -1;
			command.clear();
		}
		else if (command[0].back() == ':')
		{
			string label = command[0].substr(0, command[0].size() - 1);
			if (address.find(label) == address.end())
				address[label] = commands.size();
			else
				address[label] = -1;
			command = vector<string>(command.begin() + 1, command.end());
		}
		else if (command[0].find(':') != string::npos)
		{
			int idx = command[0].find(':');
			string label = command[0].substr(0, idx);
			if (address.find(label) == address.end())
				address[label] = commands.size();
			else
				address[label] = -1;
			command[0] = command[0].substr(idx + 1);
		}
		else if (command[1][0] == ':')
		{
			if (address.find(command[0]) == address.end())
				address[command[0]] = commands.size();
			else
				address[command[0]] = -1;
			command[1] = command[1].substr(1);
			if (command[1] == "")
				command.erase(command.begin(), command.begin() + 2);
			else
				command.erase(command.begin(), command.begin() + 1);
		}
		if (command.empty())
			return;
		if (command.size() > 4)
			for (int i = 4; i < (int)command.size(); ++i)
				command[3] += " " + command[i];
		command.resize(4);
		commands.push_back(command);
	}

	// construct the commands vector from the input file
	void constructCommands(ifstream &file)
	{
		string line;
		while (getline(file, line))
			parseCommand(line);
		file.close();
	}

	// execute the commands sequentially
	void executeCommands()
	{
		if (commands.size() >= MAX / 4)
		{
			handleExit(5);
			return;
		}

		initVars();
		cout << "Cycle info:\n";
		while (PCcurr < commands.size())
		{
			delay = 0;
			vector<string> &command = commands[PCcurr];
			if (instructions.find(command[0]) == instructions.end())
			{
				handleExit(4);
				return;
			}
			int ret = instructions[command[0]](*this, command[1], command[2], command[3]);
			if (ret > 0)
			{
				handleExit(ret);
				return;
			}
			if (ret != 0)
			{
				finishCurrDRAM(-ret - 1);
				continue;
			}
			++clockCycles;
			++commandCount[PCcurr];
			PCcurr = PCnext;
			if (!DRAM_Buffer.empty())
			{
				// first lw/sw operation after DRAM_buffer emptied
				if (currCol == -1)
					setNextDRAM(DRAM_Buffer.begin()->first, DRAM_Buffer[DRAM_Buffer.begin()->first].begin()->first);
				else if (--DRAM_Buffer[currRow][currCol].front().remainingCycles == 0)
					finishCurrDRAM();
			}
			printCycleExecution(command);
		}
		while (!DRAM_Buffer.empty())
			finishCurrDRAM();
		bufferUpdate();
		handleExit(0);
	}

	// finish the currently running DRAM instruction and set the next one
	void finishCurrDRAM(int nextRegister = -1)
	{
		auto &Q = DRAM_Buffer[currRow][currCol];
		int nextRow = currRow, nextCol = currCol;
		QElem top = DRAM_Buffer[currRow][currCol].front();
		popAndUpdate(Q, nextRow, nextCol);

		if (lastAddr==make_pair(nextRow,nextCol))
		{
			/* code */
		} else {
		clockCycles += top.remainingCycles;
		if (top.id)
		{
			registers[top.value] = data[currRow][currCol];
			if (registersAddrDRAM[top.value].first == top.issueCycle)
			{
				registersAddrDRAM[top.value] = {-1, -1};
				if (nextRegister == top.value)
					nextRegister = -1;
			}
		}
		else
			data[currRow][currCol] = top.value;
		}

		printDRAMCompletion(top.PCaddr, top.startCycle, clockCycles);
		setNextDRAM(nextRow, nextCol, nextRegister);

		//updating the last location
		lastAddr.first=currRow;
		lastAddr.second=currCol;
	}

	// set the next DRAM command to be executed (implements reordering)
	void setNextDRAM(int nextRow, int nextCol, int nextRegister = -1)
	{
		if (DRAM_Buffer.empty())
		{
			currCol = -1;
			return;
		}
		if (nextRegister != -1)
		{
			nextRow = registersAddrDRAM[nextRegister].second / ROWS;
			nextCol = (registersAddrDRAM[nextRegister].second % ROWS) / 4;
		}

		QElem top = DRAM_Buffer[nextRow][nextCol].front();
		while (top.id && registersAddrDRAM[top.value].first != top.issueCycle && popAndUpdate(DRAM_Buffer[nextRow][nextCol], nextRow, nextCol))
			top = DRAM_Buffer[nextRow][nextCol].front();

		if (DRAM_Buffer.empty())
		{
			currCol = -1;
			return;
		}
		bufferUpdate(nextRow, nextCol);
		DRAM_Buffer[currRow][currCol].front().startCycle = clockCycles + 1;
		DRAM_Buffer[currRow][currCol].front().remainingCycles = delay;
	}

	// pop the queue element and update the row and column if needed (returns false if DRAM empty after pop)
	bool popAndUpdate(queue<QElem> &Q, int &row, int &col)
	{
		Q.pop();
		if (Q.empty())
		{
			DRAM_Buffer[row].erase(col);
			if (DRAM_Buffer[row].empty())
			{
				DRAM_Buffer.erase(row);
				if (!DRAM_Buffer.empty())
					row = DRAM_Buffer.begin()->first;
			}
			if (DRAM_Buffer.empty())
				return false;
			col = DRAM_Buffer[row].begin()->first;
			return true;
		}
		return false;
	}

	// implement buffer update
	void bufferUpdate(int row = -1, int col = -1)
	{
		if (row == -1)
			delay = (currRow != -1) * row_access_delay, rowBufferUpdates += (currRow != -1);
		else if (currRow == -1)
			delay = row_access_delay + col_access_delay, ++rowBufferUpdates;
		else if (currRow != row)
			delay = 2 * row_access_delay + col_access_delay, ++rowBufferUpdates;
		else
			delay = col_access_delay;
		currRow = row, currCol = col;
	}

	// prints the cycle info of DRAM delay
	void printDRAMCompletion(int PCaddr, int begin, int end)
	{
		cout << begin << '-' << end << " (DRAM call executed): ";
		for (auto s : commands[PCaddr])
			cout << s << ' ';
		cout << "\n\n";
	}

	// print cycle info
	void printCycleExecution(vector<string> &command)
	{
		if (command[0] == "lw" || command[0] == "sw")
			cout << clockCycles << " (DRAM call queued): ";
		else
			cout << clockCycles << ": ";
		for (auto &s : command)
			cout << s << ' ';
		cout << '\n';
		printRegisters();
		cout << "\n";
	}

	// print the register data in hexadecimal
	void printRegisters()
	{
		cout << hex;
		for (int i = 0; i < 32; ++i)
			cout << registers[i] << ' ';
		cout << dec << "\n";
	}

	// initialize variables before executing commands
	void initVars()
	{
		clockCycles = 0, PCcurr = 0, rowBufferUpdates = 0, currRow = -1, currCol = -1;
		fill_n(registers, 32, 0);
		fill_n(registersAddrDRAM, 32, make_pair(-1, -1));
		data = vector<vector<int>>(ROWS, vector<int>(ROWS >> 2, 0));
		commandCount.clear();
		commandCount.assign(commands.size(), 0);
		DRAM_Buffer.clear();
		lastAddr= {-1,-1};
	}
};

int main(int argc, char *argv[])
{
	if (argc != 4)
	{
		cerr << "Required arguments: file_name ROW_ACCESS_DELAY COL_ACCESS_DELAY\n";
		return 0;
	}
	ifstream file(argv[1]);
	MIPS_Architecture *mips;
	if (file.is_open())
		try
		{
			mips = new MIPS_Architecture(file, stoi(argv[2]), stoi(argv[3]));
		}
		catch (exception &e)
		{
			cerr << "Required arguments: file_name ROW_ACCESS_DELAY COL_ACCESS_DELAY\n";
			return 0;
		}
	else
	{
		cerr << "File could not be opened. Terminating...\n";
		return 0;
	}

	mips->executeCommands();
	return 0;
}
