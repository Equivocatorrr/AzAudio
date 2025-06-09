#!/bin/sh

BuildReleaseL=0
BuildDebugL=0
BuildReleaseW=0
BuildDebugW=0

has_args=0
run_arg=0
run=0
run_debug=0
run_target=""
run_args=""
clean=0
install=0
trace=""
verbose=""

usage()
{
	echo "Usage: build.sh [clean]? [verbose]? [trace]? [install]? [All|Debug|Release|Linux|Win32|DebugL|ReleaseL|DebugW|ReleaseW]? ([run|run_debug] project_name (<arguments>)?)?"
	exit 1
}

for arg in "$@"; do
	has_args=1
	if [ $run_arg -eq 1 ]; then
		run_target="$arg"
		run_arg=2
	elif [ $run_arg -eq 2 ]; then
		run_args="$run_args $arg"
	else
		if [ "$arg" = "All" ]
		then
			BuildReleaseL=1
			BuildDebugL=1
			BuildReleaseW=1
			BuildDebugW=1
		elif [ "$arg" = "Release" ]
		then
			BuildReleaseL=1
			BuildReleaseW=1
		elif [ "$arg" = "Debug" ]
		then
			BuildDebugL=1
			BuildDebugW=1
		elif [ "$arg" = "Linux" ]
		then
			BuildReleaseL=1
			BuildDebugL=1
		elif [ "$arg" = "Win32" ]
		then
			BuildReleaseW=1
			BuildDebugW=1
		elif [ "$arg" = "ReleaseL" ]
		then
			BuildReleaseL=1
		elif [ "$arg" = "DebugL" ]
		then
			BuildDebugL=1
		elif [ "$arg" = "ReleaseW" ]
		then
			BuildReleaseW=1
		elif [ "$arg" = "DebugW" ]
		then
			BuildDebugW=1
		elif [ "$arg" = "run" ]
		then
			run_arg=1
			run=1
			run_debug=0
		elif [ "$arg" = "run_debug" ]
		then
			run_arg=1
			run=0
			run_debug=1
		elif [ "$arg" = "clean" ]
		then
			clean=1
		elif [ "$arg" = "trace" ]
		then
			trace="--trace"
		elif [ "$arg" = "verbose" ]
		then
			verbose="--verbose"
		elif [ "$arg" = "install" ]
		then
			install=1
		else
			usage
		fi
	fi
done

if [ $has_args -eq 0 ]; then
	usage
fi

NumThreads=`grep processor /proc/cpuinfo | wc -l`

abort_if_failed()
{
	if [ $? -ne 0 ]; then
		echo "$1 Aborting..."
		exit $?
	fi
}

if [ $clean -ne 0 ]; then
	echo "Cleaning..."
	( set -x; rm -rf tests/*/bin tests/*/*.log build buildDebugL buildReleaseL buildDebugW buildReleaseW )
fi

if [ $BuildDebugL -eq 1 ]
then
	buildName="Linux Debug"
	echo "Building $buildName"
	mkdir -p buildDebugL
	cd buildDebugL
	( set -x; cmake $trace -DCMAKE_BUILD_TYPE=Debug ../ )
	abort_if_failed "CMake configure failed for $buildName!"
	( set -x; cmake --build . $verbose -j $NumThreads )
	abort_if_failed "CMake build failed for $buildName!"
	cd ..
fi

if [ $BuildReleaseL -eq 1 ]
then
	buildName="Linux Release"
	echo "Building $buildName"
	mkdir -p buildReleaseL
	cd buildReleaseL
	( set -x; cmake $trace -DCMAKE_BUILD_TYPE=Release ../ )
	abort_if_failed "CMake configure failed for $buildName!"
	( set -x; cmake --build . $verbose -j $NumThreads )
	abort_if_failed "CMake build failed for $buildName!"
	if [ $install -ne 0 ]; then
		echo "To install, you need root access:"
		sudo cmake --install . $verbose
	fi
	cd ..
fi

if [ $BuildDebugW -eq 1 ]
then
	buildName="Windows Debug"
	echo "Building $buildName"
	mkdir -p buildDebugW
	cd buildDebugW
	( set -x; cmake $trace -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake ../ )
	abort_if_failed "CMake configure failed for $buildName!"
	( set -x; cmake --build . $verbose -j $NumThreads )
	abort_if_failed "CMake build failed for $buildName!"
	cd ..
fi

if [ $BuildReleaseW -eq 1 ]
then
	buildName="Windows Release"
	echo "Building $buildName"
	mkdir -p buildReleaseW
	cd buildReleaseW
	( set -x; cmake $trace -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../mingw-w64-x86_64.cmake ../ )
	abort_if_failed "CMake configure failed for $buildName!"
	( set -x; cmake --build . $verbose -j $NumThreads )
	abort_if_failed "CMake build failed for $buildName!"
	if [ $install -ne 0 ]; then
		echo "To install, you need root access:"
		sudo cmake --install . $verbose
	fi
	cd ..
fi

echo "All builds complete!"

if [ $run -ne 0 ]; then
	echo "Running \"$ $run_target $run_args\""
	cd "tests/$run_target"
	bin/$run_target $run_args
elif [ $run_debug -ne 0 ]; then
	echo "Running \"\$ $run_target $run_args\" in debug mode"
	cd "tests/$run_target"
	bin/${run_target}_debug $run_args
fi
