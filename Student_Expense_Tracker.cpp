#include <iostream>
#include <stdio.h>
#include <string>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <limits>

using namespace std;

struct Expense
{
    string date;
    string category;
    string description;
    double amount;
};

Expense addExpense()
{
    Expense expense;
    int year;
    int month;
    int date;
    
    cout<<"Enter the year: "<<endl;
    cin>>year;
    cout<<endl;
    cout<<"Enter the month"<<endl;
    cin>>month;
    cout<<endl;
    cout<<"Enter the date"<<endl;
    cin>>date;
    cout<<endl;

    string yearString = to_string(year);
    string monthString = to_string(month);
    string dateString = to_string(date);

    expense.date = dateString + "-" + monthString + "-" + yearString;

    cout<<"Enter category: "<<endl;
    getline(cin >> ws, expense.category);
    cout<<endl;

    cout<<"Enter description: "<<endl;
    getline(cin >> ws, expense.description);
    cout<<endl;

    cout<<"Enter amount (use negative for refunds): "<<endl;
    cin>>expense.amount;
    cout<<endl;
    return expense;
}

void saveExpense(const Expense& expense)
{
    ofstream expenseFile("studentExpenses.txt", ios::app);
    if (!expenseFile.is_open())
    {
        system("cls");
        cout<<"Error Opening File"<<endl;
        system("pause");
        return;
    }
    
    expenseFile << expense.date<<"|"<<expense.category<<"|"<<expense.description<<"|"<<expense.amount<<endl;
    expenseFile.close();

    return;
}

Expense splitLine(const string& report)
{
    Expense expense; 
    stringstream reportStream(report);

    string amountString;
    
    getline(reportStream, expense.date, '|');
    getline(reportStream, expense.category, '|');
    getline(reportStream, expense.description, '|');
    getline (reportStream, amountString);
    expense.amount = stod(amountString);

    return expense;
}

void viewExpense()
{
    ifstream expenseFile("studentExpenses.txt");
    if(!expenseFile.is_open())
    {
        system("cls");
        cout<<"Error Opening File"<<endl;
        system("pause");
        return;
    }
    system("cls");
    bool hasExpenses = false;
    string report;
    while (getline(expenseFile, report))
    {
        hasExpenses = true;
        Expense expense = splitLine(report);

        cout<<"Date: "<<expense.date<<endl;
        cout<<"Category: "<<expense.category<<endl;
        cout<<"Description: "<<expense.description<<endl;
        cout<<"Amount: "<<expense.amount<<"$"<<endl;
        cout<<endl;
        cout<<"-----------------------------------------------"<<endl;
        cout<<endl;
    }

    if (!hasExpenses)
    {
        cout<<"There is no expense history present in the file, pleae add an expense"<<endl;
        cout<<endl;
    }

    system("pause");
    expenseFile.close();
    return;
}

double totalAmount()
{
    ifstream expenseFile("studentExpenses.txt");
    if(!expenseFile.is_open())
    {
        system("cls");
        cout<<"Error Opening File"<<endl;
        system("pause");
        return 0;
    }
    bool hasExpenses = false;
    double totalAmount = 0;
    string report;
    while (getline(expenseFile, report))
    {
        hasExpenses = true;
        Expense expense = splitLine(report);
        totalAmount += expense.amount;
    }

     if (!hasExpenses)
    {
        cout<<"There is no expense history present in the file, pleae add an expense"<<endl;
        cout<<endl;
    }

    expenseFile.close();
    return totalAmount;

}

int main()
{
	int option=0;
    cout<<"This is Darren's Student Expense Tracker"<<endl;
    cout<<endl;
    system("pause");
    while (option != 4)
    {
        system("cls");
        cout<<"Please choose an option"<<endl;
    	cout<<"1. Add Expense"<<endl;
	    cout<<"2. View Expense"<<endl;
	    cout<<"3. View Total Spending"<<endl;
	    cout<<"4. Exit"<<endl;
        cin>>option;

        if(cin.fail()) //if cin fails due to receiving non integer input; not being able to convert it.
        {
            system("cls");
            cin.clear(); // clears the failure in cin, making it ready to accept next input
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); //ignore the fualty input in the stream, skipping it in order to recieve next input.
            cout<<"Invalid option"<<endl;
            cout<<endl;
             system("pause");
             continue; // skips everything in this itteration and moves on to the next. Basically a loop skipper.
        }

        switch (option)
        {
            case 1:
            {
                 system("cls");

         cout<<"You have chosen to add an expense"<<endl;
            Expense expense = addExpense();
            system("cls");
            saveExpense(expense);
         cout<<"Expense added: "<<endl<<endl<<"Date: "<<expense.date<<endl<<"Category: "<<expense.category<<endl<<"Description: "<<expense.description<<endl<<"Amount: "<<expense.amount<<"$"<<endl<<endl;
            system("pause");
            break;
            }   

            case 2:
            {
                  cout<<"You have chosen to view expenses"<<endl;
                  viewExpense();
                  break;
            }
            case 3:
            {
                system("cls");
                cout<<"You have chosen to view total spending"<<endl;
                cout<<endl<<endl<<endl;
                double total = totalAmount();
                cout<<"From the entire history of this file, your total expenses add up to: "<<total<<"$ !"<<endl;
                cout<<endl<<endl<<endl;
                system("pause");
                break;
            }
            case 4:
            {
                system("cls");
                cout<<"You have chosen to exit, hope to see you soon!"<<endl;
                cout<<endl;
                break;
            }
            default:
            {
                system("cls");
                cout<<"Invalid option"<<endl;
                cout<<endl;
                system("pause");
                break;
            }

        }
    }
}