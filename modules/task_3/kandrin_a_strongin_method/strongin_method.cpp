// Copyright 2022 Kandrin Alexey
#include "../../../modules/task_3/kandrin_a_strongin_method/strongin_method.h"

#include <mpi.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>


//=============================================================================
// Function : WorkSplitter::WorkSplitter
// Purpose  : Constructor.
//=============================================================================
WorkSplitter::WorkSplitter(size_t work, size_t workerCount)
    : m_workDistribution(workerCount, 0) {
  if (work <= workerCount) {
    for (size_t currentWorker = 0; currentWorker < work; ++currentWorker) {
      m_workDistribution[currentWorker] = 1;
    }
  } else {
    for (size_t currentWorker = 0; work != 0; ++currentWorker) {
      size_t workForCurrentWorker = work / workerCount;
      m_workDistribution[currentWorker] = work / workerCount;
      work -= workForCurrentWorker;
      workerCount -= 1;
    }
  }
}


//=============================================================================
// Function : GetPartWork
// Purpose  : Determining how much work a worker should do.
//=============================================================================
size_t WorkSplitter::GetPartWork(size_t workerNumber) const {
  return m_workDistribution[workerNumber];
}


//=============================================================================
// Function : GetPrevPartWork
// Purpose  : Determines how much work will be done by workers from 0 to
//            workerNumber - 1.
//=============================================================================
size_t WorkSplitter::GetPrevPartWork(size_t workerNumber) const {
  size_t work = 0;
  for (size_t i = 0; i < workerNumber; ++i) {
    work += m_workDistribution.at(i);
  }
  return work;
}


//=============================================================================
// Struct  : Sequential
// Purpose : For sequential execution strategy
//=============================================================================
struct Sequential {};


//=============================================================================
// Struct  : Parallel
// Purpose : For parallel execution strategy
//=============================================================================
struct Parallel {};


namespace {
//=============================================================================
// Function : Calculate_M
// Purpose  : Calculate the "M" estimate of the Lipschitz constant:
//  M = max{1 <= i <= n}(abs((Z_{i}.end - Z_{i}.begin) / (Y_{i}.end -
//  Y_{i}.begin))
//=============================================================================
template<class ExectionPolicy>
double Calculate_M(Function&& f, const std::vector<Segment>& y);


//=============================================================================
// Function : Calculate_M
// Purpose  : Calculate_M - sequential version
//=============================================================================
template <>
double Calculate_M<Sequential>(Function&& f, const std::vector<Segment>& y) {
  double M = 0;

  for (int i = 0; i < y.size(); ++i) {
    const double y_begin = y.at(i).begin;
    const double y_end = y.at(i).end;

    const double zDif = f(y_end) - f(y_begin);
    const double yDif = y_end - y_begin;
    const double currentMax = std::abs(zDif / yDif);
    if (currentMax > M) {
      M = currentMax;
    }
  }

  return M;
}


//=============================================================================
// Function : Calculate_M
// Purpose  : Calculate_M - parallel version
//=============================================================================
template <>
double Calculate_M<Parallel>(Function&& f, const std::vector<Segment>& y) {
  int procCount;
  MPI_Comm_size(MPI_COMM_WORLD, &procCount);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  WorkSplitter workSplitter(y.size(), procCount);

  size_t workForThisProc = workSplitter.GetPartWork(rank);
  std::vector<Segment> localY;
  
  if (rank == 0) {
    for (int procNum = 1; procNum < procCount; ++procNum) {
      size_t workForProc = workSplitter.GetPartWork(procNum);
      if (workForProc != 0) {
        size_t workForPrevProc = workSplitter.GetPrevPartWork(procNum);
        MPI_Send(&y.at(workForPrevProc), workForProc * sizeof(Segment), MPI_CHAR, procNum, 0,
                 MPI_COMM_WORLD);
      }
    }

    localY = std::vector<Segment>(y.begin(), y.begin() + workForThisProc);
  } else {
    if (workForThisProc != 0) {
      localY = std::vector<Segment>(workForThisProc);
      MPI_Recv(localY.data(), workForThisProc * sizeof(Segment), MPI_CHAR, 0, 0,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
  }

  double M = Calculate_M<Sequential>(std::forward<Function>(f), localY);

  double result;

  MPI_Reduce(&M, &result, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  
  return result;
}


//=============================================================================
// Function : Calculate_m
// Purpose  : Calculate the "m" estimate of the Lipschitz constant:
//  m = { 1 if M = 0, r * M if M > 0}, where r > 1 - parameter,
//  Z_{i}.begin = f(Y_{i}.begin),
//  Z_{i}.end = f(Y_{i}.end),
//  M = Calculate_M(...) (see above)
//=============================================================================
double Calculate_m(const double M, const double r) {
  assert(M >= 0);
  assert(r > 1);
  return (M == 0 ? 1 : r * M);
}


//=============================================================================
// Function : CalculateIndexOfMaxR
// Purpose  : Calculate all of characteristic "R".
// R(i) = m(Y_{i} - Y_{i - 1}) + sqr(Z_{i} - Z_{i - 1}) / m(Y_{i} - Y{i - 1}) -
// 2(Z_{i} + Z_{i-1}), where m = Calculate_m(...) (see above),
// Z_{i} = f(Y_{i}).
// Then return maximum of the characteristics and its index.
//=============================================================================
template <class ExecutionPolicy>
std::pair<double, int> CalculateIndexOfMaxR(Function&& f,
                                            const std::vector<Segment>& y,
                                            const double m);

//=============================================================================
// Function : CalculateIndexOfMaxR
// Purpose  : CalculateIndexOfMaxR - sequential version
//=============================================================================
template <>
std::pair<double, int> CalculateIndexOfMaxR<Sequential>(
    Function&& f, const std::vector<Segment>& y, const double m) {
  std::pair<double, int> maxR_Index(-DBL_MAX, -1);

  for (int i = 0; i < y.size(); ++i) {
    const double y_begin = y.at(i).begin;
    const double y_end = y.at(i).end;
    Debug(i, " handle: ", y_begin, ' ', y_end, '\n');

    const double yDif = y_end - y_begin;
    const double zDif = f(y_end) - f(y_begin);
    const double zSum = f(y_end) + f(y_begin);
    const double R = m * yDif + zDif * zDif / (m * yDif) - 2 * zSum;

    double& maxR = maxR_Index.first;
    int& maxIndex = maxR_Index.second;

    if (R > maxR) {
      maxR = R;
      maxIndex = i;
    }
  }
  Debug("Handle result: ", maxR_Index.first, ' ', maxR_Index.second, "\n\n");
  return maxR_Index;
}


//=============================================================================
// Function : CalculateIndexOfMaxR
// Purpose  : CalculateIndexOfMaxR - parallel version
//=============================================================================
template <>
std::pair<double, int> CalculateIndexOfMaxR<Parallel>(
    Function&& f, const std::vector<Segment>& y, const double m) {
  std::pair<double, int> maxR_Index(-DBL_MAX, -1);

  int procCount;
  MPI_Comm_size(MPI_COMM_WORLD, &procCount);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  WorkSplitter workSplitter(y.size(), procCount);

  size_t workForThisProc = workSplitter.GetPartWork(rank);
  std::vector<Segment> localY;

  if (rank == 0) {
    size_t distributedWork = workForThisProc;

    for (int procNum = 1; procNum < procCount; ++procNum) {
      size_t workForProc = workSplitter.GetPartWork(procNum);
      if (workForProc != 0) {
        MPI_Send(&y.at(distributedWork), workForProc * sizeof(Segment),
                 MPI_CHAR, procNum, 0, MPI_COMM_WORLD);
      }

      distributedWork += workForProc;
    }

    localY = std::vector<Segment>(y.begin(), y.begin() + workForThisProc);
  } else {
    if (workForThisProc != 0) {
      localY = std::vector<Segment>(workForThisProc);
      MPI_Recv(localY.data(), workForThisProc * sizeof(Segment), MPI_CHAR, 0, 0,
               MPI_COMM_WORLD, MPI_STATUSES_IGNORE);
    }
  }

  auto indexOfMaxR =
      CalculateIndexOfMaxR<Sequential>(std::forward<Function>(f), localY, m);

  Debug(indexOfMaxR.first, ' ', indexOfMaxR.second, '\n');

  if (rank == 0) {
    std::vector<std::pair<double, int>> results(
        procCount, std::pair<double, int>(-DBL_MAX, -1));
    results.at(0) = indexOfMaxR;

    for (int procNum = 1; procNum < procCount; ++procNum) {
      if (workSplitter.GetPartWork(procNum) != 0) {
        MPI_Recv(&results.at(procNum), sizeof(indexOfMaxR), MPI_CHAR, procNum,
                 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      }
    }

    Debug("pairs:\n");
    for (const auto& pair : results) {
      Debug(pair.first, ' ', pair.second, '\n');
    }

    auto iter = std::max_element(
        results.begin(), results.end(),
        [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
          return a.first < b.first;
        });
    size_t indexOfMaxElement = iter - results.begin();
    results.at(indexOfMaxElement).second +=
        workSplitter.GetPrevPartWork(indexOfMaxElement);

    Debug("Max pair = ", iter->first, ' ', iter->second, '\n');
    return *iter;

  } else {
    if (workForThisProc != 0) {
      MPI_Send(&indexOfMaxR, sizeof(indexOfMaxR), MPI_CHAR, 0, 0,
               MPI_COMM_WORLD);
    }
  }

  return {};
}


//=============================================================================
// Function : GetMin
// Purpose  : Get minimum of function f in [a; b]
//=============================================================================
template <class ExecutionPolicy>
double GetMin(Function&& f, double a, double b, double epsilon) {
  std::vector<Segment> y = {Segment{a, b}};

  const double r = 2.0;
  const size_t maxIterationCount = 100000;

  for (size_t iterationIndex = 0; iterationIndex < maxIterationCount;
       ++iterationIndex) {
    Debug("________________\nIteration index: ", iterationIndex, "\n");
    double _M = Calculate_M<ExecutionPolicy>(std::forward<Function>(f), y);
    MPI_Bcast(&_M, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    const double m = Calculate_m(_M, r);
    auto indexOfMaxR =
        CalculateIndexOfMaxR<ExecutionPolicy>(std::forward<Function>(f), y, m);

    MPI_Bcast(&indexOfMaxR, sizeof(indexOfMaxR), MPI_CHAR, 0, MPI_COMM_WORLD);
    Debug("Current indexOfMaxR = ", indexOfMaxR.first, ' ', indexOfMaxR.second,
          '\n');
    const auto& currentSegment = y.at(indexOfMaxR.second);
    const double y_begin = currentSegment.begin;
    const double y_end = currentSegment.end;
    if (y_end - y_begin < epsilon) {
      return f(y_end);
    }
    double yn =
        y_begin + (y_end - y_begin) / 2 + (f(y_end) - f(y_begin)) / (2 * m);
    y.push_back(Segment{y_begin, yn});
    y.at(indexOfMaxR.second).begin = yn;
    Debug("Segments: ");
    for (const auto& segment : y) {
      Debug(segment.begin, ' ', segment.end, "; ");
    }
    Debug("\n");
  }

  // calculation error
  return NAN;
}
}  // namespace


//=============================================================================
// Function : GetMinSequential
// Purpose  : Get minimum of function f in [a; b] - sequential version
//=============================================================================
double GetMinSequential(Function&& f, double a, double b, double epsilon) {
  return GetMin<Sequential>(std::forward<Function>(f), a, b, epsilon);
}


//=============================================================================
// Function : GetMinParallel
// Purpose  : Get minimum of function f in [a; b] - parallel version
//=============================================================================
double GetMinParallel(Function&& f, double a, double b, double epsilon) {
  return GetMin<Parallel>(std::forward<Function>(f), a, b, epsilon);
}