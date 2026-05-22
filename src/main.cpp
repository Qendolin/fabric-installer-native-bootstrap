#include "Bootstrap.h"
#include "SystemHelper.h"
#include "Logger.h"

#include <stdexcept>
#include <Userenv.h>
#include <memory>
#include <string_view>
#include <string>

#pragma comment(lib, "userenv.lib")

namespace {
	// RAII deleter for Win32 handles
	struct HandleDeleter {
		void operator()(HANDLE h) const {
			if (h && h != INVALID_HANDLE_VALUE) {
				::CloseHandle(h);
			}
		}
	};

	// RAII deleter for the Environment Block
	struct EnvBlockDeleter {
		void operator()(LPVOID p) const {
			if (p) {
				::DestroyEnvironmentBlock(p);
			}
		}
	};

	void RefreshEnvironment() {
		HANDLE rawToken = NULL;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &rawToken)) {
			return;
		}
		std::unique_ptr<void, HandleDeleter> hToken(rawToken);

		LPVOID rawEnvBlock = nullptr;
		// FALSE: build from system/user profile, not inheriting stale process env
		if (!::CreateEnvironmentBlock(&rawEnvBlock, hToken.get(), FALSE)) {
			return;
		}
		std::unique_ptr<void, EnvBlockDeleter> envBlock(rawEnvBlock);

		const wchar_t* pVar = static_cast<const wchar_t*>(envBlock.get());
		while (*pVar != L'\0') {
			std::wstring_view entry(pVar);
			const size_t eqPos = entry.find(L'=');

			if (eqPos != std::wstring_view::npos && eqPos > 0) {
				std::wstring name(entry.substr(0, eqPos));
				std::wstring value(entry.substr(eqPos + 1));
				::SetEnvironmentVariableW(name.c_str(), value.c_str());
			}

			pVar += entry.length() + 1;
		}
	}
}

_Use_decl_annotations_ int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	UNREFERENCED_PARAMETER(hInstance);
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(pCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	const auto kernel32Handle = ::GetModuleHandle(TEXT("kernel32.dll"));

	if (kernel32Handle == NULL) {
		return -1;
	}

	// Not supported on all versions of Windows 7
	typedef BOOL(WINAPI* SetDefaultDllDirectoriesFunc)(DWORD);
	const SetDefaultDllDirectoriesFunc set_default_dll_directries = reinterpret_cast<SetDefaultDllDirectoriesFunc>(::GetProcAddress(kernel32Handle, "SetDefaultDllDirectories"));

	if (set_default_dll_directries) {
		// We dont want to try and load DLLs from the current directory (Quite often the downloads dir)
		set_default_dll_directries(LOAD_LIBRARY_SEARCH_SYSTEM32);
	}

	// https://docs.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation
	::HeapSetInformation(::GetProcessHeap(), HeapEnableTerminationOnCorruption, NULL, 0);

	RefreshEnvironment();

	SystemHelper systemHelper;
	Logger logger { systemHelper };

	logger.log(L"Fabric launcher native bootstrap log:");

	auto bs = Bootstrap(systemHelper, logger);

	try {
		bs.launch();
	} catch (const std::runtime_error& error) {
		logger.log(L"a runtime error occured:");
		logger.log(error.what());
		return 1;
	}

	return 0;
}