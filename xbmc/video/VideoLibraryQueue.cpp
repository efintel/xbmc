/*
 *      Copyright (C) 2014 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "VideoLibraryQueue.h"
#include "GUIUserMessages.h"
#include "Util.h"
#include "guilib/GUIWindowManager.h"
#include "threads/SingleLock.h"
#include "video/VideoDatabase.h"
#include "video/jobs/VideoLibraryCleaningJob.h"
#include "video/jobs/VideoLibraryJob.h"
#include "video/jobs/VideoLibraryMarkWatchedJob.h"

using namespace std;

CVideoLibraryQueue::CVideoLibraryQueue()
  : CJobQueue(false, 1, CJob::PRIORITY_LOW_PAUSABLE),
    m_jobs(),
    m_cleaning(false)
{ }

CVideoLibraryQueue::~CVideoLibraryQueue()
{
  CSingleLock lock(m_critical);
  m_jobs.clear();
}

CVideoLibraryQueue& CVideoLibraryQueue::Get()
{
  static CVideoLibraryQueue s_instance;
  return s_instance;
}

void CVideoLibraryQueue::CleanLibrary(const std::set<int>& paths /* = std::set<int>() */, bool asynchronous /* = true */, CGUIDialogProgressBarHandle* progressBar /* = NULL */)
{
  CVideoLibraryCleaningJob* cleaningJob = new CVideoLibraryCleaningJob(paths, progressBar);

  if (asynchronous)
    AddJob(cleaningJob);
  else
  {
    m_cleaning = true;
    cleaningJob->DoWork();

    delete cleaningJob;
    m_cleaning = false;
  }
}

void CVideoLibraryQueue::CleanLibraryModal(const std::set<int>& paths /* = std::set<int>() */)
{
  // we can't perform a modal library cleaning if other jobs are running
  if (IsRunning())
    return;

  m_cleaning = true;
  CVideoLibraryCleaningJob cleaningJob(paths, true);
  cleaningJob.DoWork();
  m_cleaning = false;
}

void CVideoLibraryQueue::MarkAsWatched(const CFileItemPtr &item, bool watched)
{
  if (item == NULL)
    return;

  AddJob(new CVideoLibraryMarkWatchedJob(item, watched));
}

void CVideoLibraryQueue::AddJob(CVideoLibraryJob *job)
{
  if (job == NULL)
    return;

  CSingleLock lock(m_critical);
  if (!CJobQueue::AddJob(job))
    return;

  // add the job to our list of queued/running jobs
  std::string jobType = job->GetType();
  VideoLibraryJobMap::iterator jobsIt = m_jobs.find(jobType);
  if (jobsIt == m_jobs.end())
  {
    VideoLibraryJobs jobs;
    jobs.insert(job);
    m_jobs.insert(std::make_pair(jobType, jobs));
  }
  else
    jobsIt->second.insert(job);
}

void CVideoLibraryQueue::CancelJob(CVideoLibraryJob *job)
{
  if (job == NULL)
    return;

  CSingleLock lock(m_critical);
  if (job->CanBeCancelled())
    job->Cancel();

  CJobQueue::CancelJob(job);

  // remove the job from our list of queued/running jobs
  VideoLibraryJobMap::iterator jobsIt = m_jobs.find(job->GetType());
  if (jobsIt != m_jobs.end())
    jobsIt->second.erase(job);
}

void CVideoLibraryQueue::CancelAllJobs()
{
  CSingleLock lock(m_critical);
  CJobQueue::CancelJobs();

  // remove all scanning jobs
  m_jobs.clear();
}

bool CVideoLibraryQueue::IsRunning() const
{
  return CJobQueue::IsProcessing() || m_cleaning;
}

void CVideoLibraryQueue::OnJobComplete(unsigned int jobID, bool success, CJob *job)
{
  if (success)
  {
    if (QueueEmpty())
    {
      CUtil::DeleteVideoDatabaseDirectoryCache();
      CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE);
      g_windowManager.SendThreadMessage(msg);
    }
  }

  {
    CSingleLock lock(m_critical);
    // remove the job from our list of queued/running jobs
    VideoLibraryJobMap::iterator jobsIt = m_jobs.find(job->GetType());
    if (jobsIt != m_jobs.end())
      jobsIt->second.erase(static_cast<CVideoLibraryJob*>(job));
  }

  return CJobQueue::OnJobComplete(jobID, success, job);
}
