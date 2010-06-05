/*
*	Copyright (C) 2010 Thorsten Liebig (Thorsten.Liebig@gmx.de)
*
*	This program is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "operator_multithread.h"
#include "engine_multithread.h"

Operator_Multithread* Operator_Multithread::New(unsigned int numThreads)
{
	cout << "Create FDTD operator (compressed SSE + multi-threading)" << endl;
	Operator_Multithread* op = new Operator_Multithread();
	op->setNumThreads(numThreads);
	op->Init();
	return op;
}

Operator_Multithread::~Operator_Multithread()
{
}

void Operator_Multithread::setNumThreads( unsigned int numThreads )
{
	m_numThreads = numThreads;
}

Engine* Operator_Multithread::CreateEngine() const
{
	Engine_Multithread* e = Engine_Multithread::New(this,m_numThreads);
	return e;
}

Operator_Multithread::Operator_Multithread()
{
	m_CalcEC_Start=NULL;
	m_CalcEC_Stop=NULL;
}

void Operator_Multithread::Init()
{
	Operator_SSE_Compressed::Init();
	m_CalcEC_Start=NULL;
	m_CalcEC_Stop=NULL;
}

void Operator_Multithread::Reset()
{
	Operator_SSE_Compressed::Reset();

	m_thread_group.join_all();

	delete m_CalcEC_Start;m_CalcEC_Start=NULL;
	delete m_CalcEC_Stop;m_CalcEC_Stop=NULL;
}

int Operator_Multithread::CalcECOperator()
{
	if (m_numThreads == 0)
		m_numThreads = boost::thread::hardware_concurrency();

	cout << "Multithreading operator using " << m_numThreads << " threads." << std::endl;

	m_thread_group.join_all();
	delete m_CalcEC_Start;m_CalcEC_Start = new boost::barrier(m_numThreads+1); // numThread workers + 1 controller
	delete m_CalcEC_Stop;m_CalcEC_Stop = new boost::barrier(m_numThreads+1); // numThread workers + 1 controller

	unsigned int linesPerThread = round((float)numLines[0] / (float)m_numThreads);
	for (unsigned int n=0; n<m_numThreads; n++)
	{
		unsigned int start = n * linesPerThread;
		unsigned int stop = (n+1) * linesPerThread - 1;
		if (n == m_numThreads-1) // last thread
			stop = numLines[0]-1;

		boost::thread *t = new boost::thread( Operator_Thread(this,start,stop,n) );
		m_thread_group.add_thread( t );
	}

	return Operator_SSE_Compressed::CalcECOperator();
}

bool Operator_Multithread::Calc_EC()
{
	if (CSX==NULL) {cerr << "CartOperator::Calc_EC: CSX not given or invalid!!!" << endl; return false;}

	MainOp->SetPos(0,0,0);

	m_CalcEC_Start->wait();

	m_CalcEC_Stop->wait();

	return true;
}

Operator_Thread::Operator_Thread( Operator_Multithread* ptr, unsigned int start, unsigned int stop, unsigned int threadID )
{
	m_start=start;
	m_stop=stop;
	m_threadID=threadID;
	m_OpPtr = ptr;
}

void Operator_Thread::operator()()
{
	//************** calculate EC (Calc_EC) ***********************//
	m_OpPtr->m_CalcEC_Start->wait();
	unsigned int ipos;
	unsigned int pos[3];
	double inEC[4];
	for (int n=0;n<3;++n)
	{
		for (pos[0]=m_start;pos[0]<=m_stop;++pos[0])
		{
			for (pos[1]=0;pos[1]<m_OpPtr->numLines[1];++pos[1])
			{
				for (pos[2]=0;pos[2]<m_OpPtr->numLines[2];++pos[2])
				{
					m_OpPtr->Calc_ECPos(n,pos,inEC);
					ipos = m_OpPtr->MainOp->GetPos(pos[0],pos[1],pos[2]);
					m_OpPtr->EC_C[n][ipos]=inEC[0];
					m_OpPtr->EC_G[n][ipos]=inEC[1];
					m_OpPtr->EC_L[n][ipos]=inEC[2];
					m_OpPtr->EC_R[n][ipos]=inEC[3];
				}
			}
		}
	}
	m_OpPtr->m_CalcEC_Stop->wait();
}
