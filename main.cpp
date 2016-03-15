#include <conio.h>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
#include <Windows.h>
#include <iphlpapi.h>
#include <Icmpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

class IcmpHandle
{
public:
	IcmpHandle()
		: m_handle(::IcmpCreateFile())
	{ }

	~IcmpHandle()
	{
		::IcmpCloseHandle(m_handle);
	}

	operator HANDLE()
	{
		return m_handle;
	}

private:
	HANDLE m_handle;
};

class Results
{
public:
	Results()
	{
		::InitializeCriticalSection(&m_cs);

		m_file.open("results.txt", std::ios_base::app);
	}

	~Results()
	{
		WriteResults();
		::DeleteCriticalSection(&m_cs);
	}

	void AddResult(FILETIME ft, uint64_t responseTime)
	{
		m_results.emplace_back(ft, responseTime);
	}

	std::size_t Count() const
	{
		return m_results.size();
	}

	void WriteResults()
	{
		for (auto && result : m_results)
		{
			SYSTEMTIME systime;

			::FileTimeToSystemTime(&result.first, &systime);

			using std::setfill;
			using std::setw;

			m_file << systime.wYear << "-"
				<< setw(2) << setfill('0') << systime.wMonth << "-"
				<< setw(2) << setfill('0') << systime.wDay << " "
				<< setw(2) << setfill('0') << systime.wHour << ":"
				<< setw(2) << setfill('0') << systime.wMinute << ":"
				<< setw(2) << setfill('0') << systime.wSecond << "."
				<< systime.wMilliseconds << " ";

			m_file << result.second << "\n";
		}

		m_results.clear();
	}

	friend class Lock;

	class Lock
	{
	public:
		Lock(Results & results)
			: m_results(results)
		{
			::EnterCriticalSection(&m_results.m_cs);
		}

		~Lock()
		{
			::LeaveCriticalSection(&m_results.m_cs);
		}

		Lock(const Lock &) = delete;
		Lock(Lock &&) = delete;
		Lock &operator=(const Lock &) = delete;
		Lock &operator=(Lock &&) = delete;

	private:
		Results & m_results;
	};

private:
	std::vector<std::pair<FILETIME, uint64_t>> m_results;
	CRITICAL_SECTION m_cs;
	std::ofstream m_file;
};

const char * g_pingAddress = "8.8.8.8";
Results g_results;

uint64_t FileTimeToUint(const FILETIME & ft)
{
	return (uint64_t)ft.dwLowDateTime + ((uint64_t)ft.dwHighDateTime << 32ULL);
}

DWORD WINAPI ping(LPVOID param)
{
	IcmpHandle handle;

	char buffer[64];
	std::memset(buffer, 0xAA, sizeof(buffer));

	std::size_t replySize = sizeof(ICMP_ECHO_REPLY) + sizeof(buffer);
	std::unique_ptr<char[]> reply(new char [replySize]);

	IPAddr addr = inet_addr(g_pingAddress);

	FILETIME before, after;

	::GetSystemTimeAsFileTime(&before);

	DWORD result =
		::IcmpSendEcho(handle, addr, buffer, sizeof(buffer), NULL, reply.get(), replySize, 1000);

	::GetSystemTimeAsFileTime(&after);

	if (result == 0)
	{
		std::cout << "ping timeout\n";
	}
	else
	{
		uint64_t difference = FileTimeToUint(after) - FileTimeToUint(before);
		difference /= 10000;

		SYSTEMTIME systime;
		FileTimeToSystemTime(&before, &systime);

		//using std::setfill;
		//using std::setw;
		//
		//std::cout << systime.wYear << "-"
		//	<< setw(2) << setfill('0') << systime.wMonth << "-"
		//	<< setw(2) << setfill('0') << systime.wDay << " "
		//	<< setw(2) << setfill('0') << systime.wHour << ":"
		//	<< setw(2) << setfill('0') << systime.wMinute << ":"
		//	<< setw(2) << setfill('0') << systime.wSecond << "."
		//	<< systime.wMilliseconds << " ";
		//
		//std::cout << "ping response = " << difference << "\n";

		Results::Lock lock(g_results);
		g_results.AddResult(before, difference);
	}

	return 0;
}

DWORD WINAPI pinger(LPVOID param)
{
	HANDLE timer = ::CreateWaitableTimer(NULL, FALSE, NULL);

	LARGE_INTEGER interval;

	// 1 second
	interval.QuadPart = -10000000LL;

	::SetWaitableTimer(timer, &interval, 5000, NULL, NULL, FALSE);

	static int z = 0;
	while (::WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0)
	{
		if (g_results.Count() > 100)
		{
			Results::Lock lock(g_results);
			g_results.WriteResults();
		}

		::CreateThread(NULL, 0, &ping, NULL, 0, NULL);
	}

	return 0;
}

int main()
{
	std::cout << "Press any key to exit...\n";

	::CreateThread(NULL, 0, &pinger, NULL, 0, NULL);

	while (!_kbhit())
		;

	return 0;
}
